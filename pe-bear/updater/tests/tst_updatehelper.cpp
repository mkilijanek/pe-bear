/*
 * Covers the helper's orchestration: the order of the refusals, the waiting,
 * and what happens to the installation when each step fails.
 *
 * Not an isolated unit test on purpose. The platform installer, the
 * transaction, the journal and the extractor are all the real ones, driven
 * over a fake filesystem -- because the thing worth testing here is not any
 * one of them but the claim they combine to make: that a refusal leaves the
 * installation byte-for-byte as it was, and that a failure after activation
 * puts the previous build back. Mocking the installer would test that the
 * calls were made in order, which is the easy half.
 *
 * Only the process boundary and the digest are faked, because neither can be
 * driven otherwise: one needs real processes, the other a real file.
 */
#include <QtTest>
#include "../UpdateHelper.h"
#include "../DirectoryInstaller.h"
#include "../StartupHandshake.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	const char* ROOT    = "/u/.pe-bear/updates";
	const char* PKG     = "/u/.pe-bear/updates/downloads/r/pkg.zip";
	const char* TARGET  = "/opt/pe-bear";
	const char* VERSION = "0.7.3";
	const qint64 PARENT_PID = 4242;

	QString exeName() { return DirectoryInstaller::applicationFileName(); }
	QString targetExe() { return QLatin1String(TARGET) + QLatin1Char('/') + exeName(); }
	QString digest() { return QString(64, QLatin1Char('d')); }

	/** An archive described entirely in memory. */
	class FakeArchiveReader : public IArchiveReader
	{
	public:
		void addFile(const QString &path, const QByteArray &body)
		{
			m_entries.append(ArchiveEntry(path, ArchiveEntry::KindFile, body.size(), body.size()));
			m_bodies.insert(path, body);
		}
		virtual bool open(const QString &) { return true; }
		virtual void close() {}
		virtual QList<ArchiveEntry> entries() const { return m_entries; }
		virtual QByteArray readEntry(const QString &path) { return m_bodies.value(path); }
		virtual QString lastError() const { return QLatin1String("fake reader"); }

	private:
		QList<ArchiveEntry> m_entries;
		QMap<QString, QByteArray> m_bodies;
	};

	/**
	 * A process that is running until it is not.
	 *
	 * Time only moves when the subject sleeps, so a test of the timeout costs
	 * nothing and cannot be flaky -- the alternative is a real wait, which
	 * would make this suite take as long as the timeout it is checking.
	 */
	class FakeProcessProbe : public IProcessProbe
	{
	public:
		FakeProcessProbe() : m_now(0), m_identifies(true), m_exitsAt(0), m_sleeps(0) {}

		/** Already gone by the time the helper looks. */
		void setAlreadyExited() { m_identifies = false; }
		/** Still running after @p ms of waiting. */
		void setExitsAt(qint64 ms) { m_exitsAt = ms; }
		void setNeverExits() { m_exitsAt = -1; }
		int sleepCalls() const { return m_sleeps; }
		qint64 waitedMs() const { return m_now; }

		virtual ProcessIdentity identify(qint64 pid)
		{
			ProcessIdentity who;
			if (!m_identifies) return who;
			who.pid = pid;
			who.startToken = QLatin1String("fake");
			return who;
		}
		virtual bool isRunning(const ProcessIdentity &who) const
		{
			if (!who.isValid()) return false;
			if (m_exitsAt < 0) return true;
			return m_now < m_exitsAt;
		}
		virtual void sleep(int ms) { m_now += ms; m_sleeps++; }
		virtual qint64 elapsedMs() const { return m_now; }
		virtual QString lastError() const { return QLatin1String("fake probe"); }

	private:
		qint64 m_now;
		bool m_identifies;
		qint64 m_exitsAt;
		int m_sleeps;
	};

	/**
	 * Stands in for the newly installed build.
	 *
	 * It answers the handshake by actually reading the request out of the fake
	 * filesystem and writing a response, rather than by being told what the
	 * verdict should be. That way the nonce and version checks are exercised
	 * for real: a test that injected "Accepted" would pass just as happily
	 * against a handshake that never compared anything.
	 */
	class FakeProcessLauncher : public IProcessLauncher
	{
	public:
		enum Behaviour {
			AnswersCorrectly,
			AnswersWithAnotherNonce,
			AnswersWithAnotherVersion,
			AnswersReportingFailure,
			AnswersNothing,
			WillNotStart,
			NeverFinishes,
			Crashes
		};

		explicit FakeProcessLauncher(IFileSystem *fs)
			: m_fs(fs), m_behaviour(AnswersCorrectly), m_runs(0),
			m_detached(0), m_detachedOk(true) {}

		void setBehaviour(Behaviour b) { m_behaviour = b; }
		void setDetachedFails() { m_detachedOk = false; }
		int runs() const { return m_runs; }
		int detachedStarts() const { return m_detached; }
		QString ranExe() const { return m_ranExe; }
		QStringList ranArgs() const { return m_ranArgs; }
		QString detachedExe() const { return m_detachedExe; }

		virtual Result runAndWait(const QString &exe, const QStringList &args,
			const QString &, int)
		{
			m_runs++;
			m_ranExe = exe;
			m_ranArgs = args;

			Result r;
			if (m_behaviour == WillNotStart) return r;
			r.started = true;
			if (m_behaviour == NeverFinishes) return r;
			r.exited = true;
			if (m_behaviour == Crashes) {
				r.crashed = true;
				r.exitCode = -1;
				return r;
			}
			r.exitCode = 0;
			if (m_behaviour != AnswersNothing) answer(args);
			return r;
		}

		virtual bool startDetached(const QString &exe, const QStringList &, const QString &)
		{
			m_detached++;
			m_detachedExe = exe;
			return m_detachedOk;
		}
		virtual QString lastError() const { return QLatin1String("fake launcher"); }

	private:
		void answer(const QStringList &args)
		{
			if (args.size() < 2) return;
			const QString requestPath = args.at(1);

			bool ok = false;
			const StartupHandshake::Request request =
				StartupHandshake::Request::fromJson(m_fs->readFile(requestPath), &ok);
			if (!ok) return;

			StartupHandshake::Response response;
			response.token = (m_behaviour == AnswersWithAnotherNonce)
				? QString(32, QLatin1Char('0')) : request.token;
			response.version = (m_behaviour == AnswersWithAnotherVersion)
				? QLatin1String("9.9.9") : request.expectedVersion;
			response.started = (m_behaviour != AnswersReportingFailure);
			if (!response.started) response.detail = QLatin1String("could not read its settings");

			m_fs->writeFile(request.responsePath, response.toJson());
		}

		IFileSystem *m_fs;
		Behaviour m_behaviour;
		int m_runs;
		int m_detached;
		bool m_detachedOk;
		QString m_ranExe;
		QStringList m_ranArgs;
		QString m_detachedExe;
	};

	/** The digest has to come from somewhere that is not a real file. */
	class TestableHelper : public UpdateHelper
	{
	public:
		TestableHelper(IFileSystem *fs, PlatformInstaller *platform, IProcessProbe *probe,
				IProcessLauncher *launcher, const UpdatePaths &paths,
				const Limits &limits = Limits())
			: UpdateHelper(fs, platform, probe, launcher, paths, limits),
			m_digest(digest()) {}

		void setDigestOnDisk(const QString &d) { m_digest = d; }

	protected:
		virtual QString computeDigest(const QString &) const { return m_digest; }

	private:
		QString m_digest;
	};

}; // namespace

class TestUpdateHelper : public QObject
{
	Q_OBJECT

private slots:
	void init();

	void installsValidatesAndCommits();
	void restartsPeBearWhenAsked();
	void doesNotRestartWhenNotAsked();
	void aFailedRestartDoesNotUndoAGoodUpdate();

	void refusesAMalformedInstruction();
	void refusesAStaleInstruction();
	void refusesAnInstructionDatedAhead();
	void acceptsASlightlySkewedClock();

	void refusesAMissingPackage();
	void refusesAPackageOfTheWrongSize();
	void refusesAPackageWithTheWrongDigest();
	void refusesAPackageOutsideTheUpdaterDirectory();
	void refusesAPackageThatIsADirectory();

	void refusesATargetThatIsNotADirectory();
	void refusesATargetInsideTheUpdaterDirectory();
	void refusesATargetWithNoExecutable();
	void refusesATargetTheUserCannotWriteTo();

	void waitsForPeBearToClose();
	void proceedsWhenPeBearHasAlreadyGone();
	void refusesWhenPeBearNeverCloses();

	void rollsBackWhenTheNewBuildWillNotStart();
	void rollsBackWhenTheNewBuildSaysNothing();
	void rollsBackWhenTheNonceDoesNotMatch();
	void rollsBackWhenTheVersionDoesNotMatch();
	void rollsBackWhenTheNewBuildReportsFailure();
	void rollsBackWhenTheNewBuildCrashes();
	void rollsBackWhenTheNewBuildNeverFinishes();

	void aFailedBackupIsACleanRefusal();
	void needsAttentionWhenTheRestoreAlsoFails();
	void refusesAnArchiveThatIsNotPeBearWithoutTouchingTheInstallation();

	void exitCodesAreDistinctAndStable();
	void everyRefusalIsReportedAsLeavingThingsUntouched();

private:
	HelperHandoff goodHandoff() const;
	/** Asserts the installation is exactly as the test set it up. */
	void assertUntouched();

	FakeFileSystem m_fs;
	FakeArchiveReader m_reader;
};

HelperHandoff TestUpdateHelper::goodHandoff() const
{
	HelperHandoff h;
	h.runId = QString(32, QLatin1Char('a'));
	h.packagePath = QLatin1String(PKG);
	h.packageSha256 = digest();
	h.packageSize = 13;
	h.targetDir = QLatin1String(TARGET);
	h.expectedVersion = QLatin1String(VERSION);
	h.parentPid = PARENT_PID;
	h.relaunch = false;
	h.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
	h.assetUrl = QLatin1String("https://example.invalid/pkg.zip");
	h.releaseTag = QLatin1String("v0.7.3");
	h.assetName = QLatin1String("pkg.zip");
	return h;
}

void TestUpdateHelper::init()
{
	m_fs = FakeFileSystem();
	m_reader = FakeArchiveReader();
	m_fs.setWritableDefault(true);

	/* 13 bytes, matching the size in the handoff. */
	m_fs.addFile(QLatin1String(PKG), QByteArray("package bytes"));
	/* The installation that is about to be replaced. */
	m_fs.addFile(targetExe(), QByteArray("the old build"));
	m_fs.addFile(QLatin1String(TARGET) + QLatin1String("/tags/notes.tag"), QByteArray("keep me"));

	/* What the package contains. */
	m_reader.addFile(exeName(), QByteArray("the new build"));
	m_reader.addFile(QLatin1String("readme.txt"), QByteArray("0.7.3"));
}

void TestUpdateHelper::assertUntouched()
{
	QVERIFY2(m_fs.hasFile(targetExe()), "the installed executable is gone");
	QCOMPARE(m_fs.readFile(targetExe()), QByteArray("the old build"));
	QVERIFY2(m_fs.hasFile(QLatin1String(TARGET) + QLatin1String("/tags/notes.tag")),
		"an unrelated file in the installation is gone");
}

//---------------------------------------------------------------- happy path

void TestUpdateHelper::installsValidatesAndCommits()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	const UpdateHelper::Result r = helper.run(goodHandoff());
	QVERIFY2(r == UpdateHelper::Succeeded,
		qPrintable(UpdateHelper::resultToString(r) + QLatin1String(": ") + helper.lastError()));

	/* The new build is in place... */
	QCOMPARE(m_fs.readFile(targetExe()), QByteArray("the new build"));
	/* ...the handshake was actually run against it... */
	QCOMPARE(launcher.runs(), 1);
	QCOMPARE(launcher.ranExe(), targetExe());
	QCOMPARE(launcher.ranArgs().value(0), QLatin1String("--update-handshake"));
	/* ...and the request did not live inside the directory being replaced. */
	QVERIFY(!launcher.ranArgs().value(1).startsWith(QLatin1String(TARGET)));
	/* A transaction was opened and the journal records it. */
	QVERIFY(!helper.transactionId().isEmpty());
}

void TestUpdateHelper::restartsPeBearWhenAsked()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.relaunch = true;
	QCOMPARE(helper.run(h), UpdateHelper::Succeeded);

	QCOMPARE(launcher.detachedStarts(), 1);
	QCOMPARE(launcher.detachedExe(), targetExe());
}

void TestUpdateHelper::doesNotRestartWhenNotAsked()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::Succeeded);
	QCOMPARE(launcher.detachedStarts(), 0);
}

void TestUpdateHelper::aFailedRestartDoesNotUndoAGoodUpdate()
{
	/* The update is committed by this point. Treating "could not restart" as
	   a failure would invite undoing a working installation over it. */
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setDetachedFails();

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.relaunch = true;
	QCOMPARE(helper.run(h), UpdateHelper::Succeeded);
	QCOMPARE(m_fs.readFile(targetExe()), QByteArray("the new build"));
}

//---------------------------------------------------------------- the instruction

void TestUpdateHelper::refusesAMalformedInstruction()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.packageSha256 = QLatin1String("short");

	QCOMPARE(helper.run(h), UpdateHelper::RefusedInvalidRequest);
	QVERIFY(helper.transactionId().isEmpty());
	QCOMPARE(launcher.runs(), 0);
	assertUntouched();
}

void TestUpdateHelper::refusesAStaleInstruction()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	const QDateTime now = QDateTime::currentDateTimeUtc();
	h.createdAtUtc = now.addSecs(-(HelperHandoff::MAX_AGE_SECONDS + 60)).toString(Qt::ISODate);
	helper.setNow(now);

	QCOMPARE(helper.run(h), UpdateHelper::RefusedStaleRequest);
	assertUntouched();
}

void TestUpdateHelper::refusesAnInstructionDatedAhead()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	const QDateTime now = QDateTime::currentDateTimeUtc();
	h.createdAtUtc = now.addSecs(3600).toString(Qt::ISODate);
	helper.setNow(now);

	QCOMPARE(helper.run(h), UpdateHelper::RefusedStaleRequest);
	assertUntouched();
}

void TestUpdateHelper::acceptsASlightlySkewedClock()
{
	/* A minute of skew between writing the instruction and reading it is
	   ordinary, and refusing it would make the updater fail on machines whose
	   clocks are merely imperfect. */
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	const QDateTime now = QDateTime::currentDateTimeUtc();
	h.createdAtUtc = now.addSecs(60).toString(Qt::ISODate);
	helper.setNow(now);

	QCOMPARE(helper.run(h), UpdateHelper::Succeeded);
}

//---------------------------------------------------------------- the package

void TestUpdateHelper::refusesAMissingPackage()
{
	m_fs.removeFile(QLatin1String(PKG));

	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RefusedPackageMismatch);
	assertUntouched();
}

void TestUpdateHelper::refusesAPackageOfTheWrongSize()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.packageSize = 999999;

	QCOMPARE(helper.run(h), UpdateHelper::RefusedPackageMismatch);
	assertUntouched();
}

void TestUpdateHelper::refusesAPackageWithTheWrongDigest()
{
	/* The point of re-verifying: PE-bear hashed this file when it arrived,
	   which was a statement about the past. */
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));
	helper.setDigestOnDisk(QString(64, QLatin1Char('e')));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RefusedPackageMismatch);
	QVERIFY(helper.lastError().contains(QLatin1String("digest")));
	assertUntouched();
}

void TestUpdateHelper::refusesAPackageOutsideTheUpdaterDirectory()
{
	/* Otherwise the helper is a general-purpose archive extractor that
	   happens to run with the user's rights. */
	m_fs.addFile(QLatin1String("/tmp/elsewhere/pkg.zip"), QByteArray("package bytes"));

	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.packagePath = QLatin1String("/tmp/elsewhere/pkg.zip");

	QCOMPARE(helper.run(h), UpdateHelper::RefusedPackageMismatch);
	QVERIFY(helper.lastError().contains(QLatin1String("outside")));
	assertUntouched();
}

void TestUpdateHelper::refusesAPackageThatIsADirectory()
{
	m_fs.addDir(QLatin1String(ROOT) + QLatin1String("/downloads/r/dir.zip"));

	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.packagePath = QLatin1String(ROOT) + QLatin1String("/downloads/r/dir.zip");

	QCOMPARE(helper.run(h), UpdateHelper::RefusedPackageMismatch);
	assertUntouched();
}

//---------------------------------------------------------------- the target

void TestUpdateHelper::refusesATargetThatIsNotADirectory()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.targetDir = targetExe(); /* the executable, not its directory */

	QCOMPARE(helper.run(h), UpdateHelper::RefusedTarget);
	assertUntouched();
}

void TestUpdateHelper::refusesATargetInsideTheUpdaterDirectory()
{
	/* Staging and the backups live there. Replacing one of those with a build
	   would make the transaction record describe something that is gone. */
	m_fs.addFile(QLatin1String(ROOT) + QLatin1String("/staging/x/") + exeName(),
		QByteArray("staged"));

	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.targetDir = QLatin1String(ROOT) + QLatin1String("/staging/x");

	QCOMPARE(helper.run(h), UpdateHelper::RefusedTarget);
	QVERIFY(helper.lastError().contains(QLatin1String("updater")));
	assertUntouched();
}

void TestUpdateHelper::refusesATargetWithNoExecutable()
{
	m_fs.addDir(QLatin1String("/opt/something-else"));

	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	HelperHandoff h = goodHandoff();
	h.targetDir = QLatin1String("/opt/something-else");

	QCOMPARE(helper.run(h), UpdateHelper::RefusedTarget);
	assertUntouched();
}

void TestUpdateHelper::refusesATargetTheUserCannotWriteTo()
{
	/* Reported, never escalated. Asking for administrator rights from an
	   update check is how an updater becomes worth attacking. */
	m_fs.setWritable(QLatin1String(TARGET), false);

	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	FakeProcessLauncher launcher(&m_fs);
	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RefusedTarget);
	assertUntouched();
}

//---------------------------------------------------------------- waiting

void TestUpdateHelper::waitsForPeBearToClose()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setExitsAt(2000); /* closes after two seconds of waiting */
	FakeProcessLauncher launcher(&m_fs);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::Succeeded);
	QVERIFY2(probe.sleepCalls() > 0, "it did not wait at all");
	QVERIFY(probe.waitedMs() >= 2000);
}

void TestUpdateHelper::proceedsWhenPeBearHasAlreadyGone()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::Succeeded);
	QCOMPARE(probe.sleepCalls(), 0);
}

void TestUpdateHelper::refusesWhenPeBearNeverCloses()
{
	/* And in particular does not terminate it: there is no code here that
	   could, and the installation is left alone. */
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setNeverExits();
	FakeProcessLauncher launcher(&m_fs);

	UpdateHelper::Limits limits;
	limits.parentExitTimeoutMs = 3000;
	limits.parentPollIntervalMs = 250;

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)), limits);

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RefusedParentStillRunning);
	QVERIFY(helper.transactionId().isEmpty());
	assertUntouched();
	/* It gave up at the deadline rather than spinning. */
	QVERIFY(probe.waitedMs() >= 3000);
	QVERIFY(probe.waitedMs() < 3000 + 2 * limits.parentPollIntervalMs);
}

//---------------------------------------------------------------- validation

void TestUpdateHelper::rollsBackWhenTheNewBuildWillNotStart()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setBehaviour(FakeProcessLauncher::WillNotStart);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RolledBack);
	/* The previous build is back, with the files that were beside it. */
	QCOMPARE(m_fs.readFile(targetExe()), QByteArray("the old build"));
	assertUntouched();
}

void TestUpdateHelper::rollsBackWhenTheNewBuildSaysNothing()
{
	/* A build that starts and dies has validated nothing. Committing on
	   elapsed time would destroy the only working copy. */
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setBehaviour(FakeProcessLauncher::AnswersNothing);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RolledBack);
	assertUntouched();
}

void TestUpdateHelper::rollsBackWhenTheNonceDoesNotMatch()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setBehaviour(FakeProcessLauncher::AnswersWithAnotherNonce);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RolledBack);
	assertUntouched();
}

void TestUpdateHelper::rollsBackWhenTheVersionDoesNotMatch()
{
	/* It started, but it is not what was installed -- which usually means the
	   activation put the wrong thing in place. */
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setBehaviour(FakeProcessLauncher::AnswersWithAnotherVersion);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RolledBack);
	assertUntouched();
}

void TestUpdateHelper::rollsBackWhenTheNewBuildReportsFailure()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setBehaviour(FakeProcessLauncher::AnswersReportingFailure);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RolledBack);
	assertUntouched();
}

void TestUpdateHelper::rollsBackWhenTheNewBuildCrashes()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setBehaviour(FakeProcessLauncher::Crashes);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RolledBack);
	QVERIFY(helper.lastError().contains(QLatin1String("crashed")));
	assertUntouched();
}

void TestUpdateHelper::rollsBackWhenTheNewBuildNeverFinishes()
{
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setBehaviour(FakeProcessLauncher::NeverFinishes);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RolledBack);
	assertUntouched();
}

void TestUpdateHelper::aFailedBackupIsACleanRefusal()
{
	/* The installation could not be moved aside, which means it was never
	   moved -- so this must come back as a rollback that really did leave
	   everything in place, not as something needing attention. */
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	m_fs.failForPath(QLatin1String("movePath"), QLatin1String(TARGET));

	QCOMPARE(helper.run(goodHandoff()), UpdateHelper::RolledBack);
	QCOMPARE(launcher.runs(), 0);
	assertUntouched();
}

void TestUpdateHelper::needsAttentionWhenTheRestoreAlsoFails()
{
	/* The one outcome that needs a person, and it has to be distinguishable
	   from a clean rollback because the advice differs: one is "nothing
	   happened", the other is "reinstall".
	
	   Reached by letting the install succeed, failing the handshake, and then
	   breaking the move that puts the previous build back. The call index is
	   what makes that specific: 1 is the backup, 2 is the activation, 3 is the
	   first undo. Asserted below rather than assumed. */
	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);
	launcher.setBehaviour(FakeProcessLauncher::AnswersNothing);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	m_fs.failOnCall(QLatin1String("movePath"), 3);

	const UpdateHelper::Result r = helper.run(goodHandoff());
	QCOMPARE(r, UpdateHelper::NeedsAttention);
	QVERIFY(launcher.runs() == 1);
	/* And it says so: the message has to carry both what failed and that the
	   restore did not finish, or the user is told to reinstall with no reason
	   given. */
	QVERIFY(helper.lastError().contains(QLatin1String("restore did not finish")));
}

void TestUpdateHelper::refusesAnArchiveThatIsNotPeBearWithoutTouchingTheInstallation()
{
	/* A package can verify by digest and still be the wrong package. Finding
	   that out after the installation has been moved aside is strictly worse,
	   so the staged layout is checked first. */
	m_reader = FakeArchiveReader();
	m_reader.addFile(QLatin1String("some-other-program"), QByteArray("not pe-bear"));

	DirectoryInstaller platform(&m_fs, &m_reader);
	FakeProcessProbe probe;
	probe.setAlreadyExited();
	FakeProcessLauncher launcher(&m_fs);

	TestableHelper helper(&m_fs, &platform, &probe, &launcher,
		UpdatePaths(QLatin1String(ROOT), QLatin1String(ROOT)));

	const UpdateHelper::Result r = helper.run(goodHandoff());
	QVERIFY2(r == UpdateHelper::RolledBack, qPrintable(UpdateHelper::resultToString(r)));
	QCOMPARE(launcher.runs(), 0);
	assertUntouched();
}

//---------------------------------------------------------------- reporting

void TestUpdateHelper::exitCodesAreDistinctAndStable()
{
	/* PE-bear reads these back, so they are an interface. Distinct because a
	   shared code would make two different outcomes indistinguishable. */
	QSet<int> codes;
	for (int i = 0; i < UpdateHelper::RESULTS_COUNT; i++) {
		const UpdateHelper::Result r = static_cast<UpdateHelper::Result>(i);
		codes.insert(UpdateHelper::resultToExitCode(r));
		QVERIFY(!UpdateHelper::resultToString(r).isEmpty());
		QVERIFY(UpdateHelper::resultToString(r) != QLatin1String("Invalid"));
		QVERIFY(!UpdateHelper::resultMessage(r).isEmpty());
	}
	QCOMPARE(codes.size(), int(UpdateHelper::RESULTS_COUNT));

	/* Success is zero, because that is what a shell expects. */
	QCOMPARE(UpdateHelper::resultToExitCode(UpdateHelper::Succeeded), 0);
}

void TestUpdateHelper::everyRefusalIsReportedAsLeavingThingsUntouched()
{
	/* The GUI tells the user "nothing was changed" based on this, so it has
	   to be true of exactly the outcomes that claim it. */
	QVERIFY(UpdateHelper::leftUntouched(UpdateHelper::RefusedInvalidRequest));
	QVERIFY(UpdateHelper::leftUntouched(UpdateHelper::RefusedStaleRequest));
	QVERIFY(UpdateHelper::leftUntouched(UpdateHelper::RefusedPackageMismatch));
	QVERIFY(UpdateHelper::leftUntouched(UpdateHelper::RefusedTarget));
	QVERIFY(UpdateHelper::leftUntouched(UpdateHelper::RefusedParentStillRunning));

	QVERIFY(!UpdateHelper::leftUntouched(UpdateHelper::Succeeded));
	QVERIFY(!UpdateHelper::leftUntouched(UpdateHelper::RolledBack));
	QVERIFY(!UpdateHelper::leftUntouched(UpdateHelper::NeedsAttention));
	QVERIFY(!UpdateHelper::leftUntouched(UpdateHelper::InternalError));
}

QTEST_MAIN(TestUpdateHelper)
#include "tst_updatehelper.moc"
