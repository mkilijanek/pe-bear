/*
 * Tests for MacOSInstaller - the macOS-specific platform installer.
 */
#include <QtTest>
#include "../MacOSInstaller.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	class FakeArchiveReader : public IArchiveReader
	{
	public:
		FakeArchiveReader() : m_openOk(true) {}

		void addFile(const QString &path, const QByteArray &body = QByteArray("x"))
		{
			m_entries.append(ArchiveEntry(path, ArchiveEntry::KindFile, body.size(), body.size()));
			m_bodies.insert(path, body);
		}
		void addDir(const QString &path)
		{
			m_entries.append(ArchiveEntry(path, ArchiveEntry::KindDir));
		}
		void setOpenFails() { m_openOk = false; }

		virtual bool open(const QString &) { return m_openOk; }
		virtual void close() {}
		virtual QList<ArchiveEntry> entries() const { return m_entries; }
		virtual QByteArray readEntry(const QString &path) { return m_bodies.value(path); }
		virtual QString lastError() const { return QLatin1String("fake reader"); }

	private:
		QList<ArchiveEntry> m_entries;
		QMap<QString, QByteArray> m_bodies;
		bool m_openOk;
	};

	const char* PKG = "/u/.pe-bear/updates/downloads/r/pkg.zip";
	const char* STAGING = "/u/.pe-bear/updates/staging/tx-1";
	const char* TARGET = "/Applications/PE-bear.app";

	VerifiedUpdate aVerifiedUpdate()
	{
		ReleaseAsset asset;
		asset.name = QLatin1String("pkg.zip");
		asset.downloadUrl = QUrl(QLatin1String("https://example.invalid/pkg.zip"));
		asset.size = 2048;
		asset.sha256 = QString(64, QLatin1Char('c'));

		ReleaseInfo release;
		release.tagName = QLatin1String("v0.7.3");
		release.version = Version::fromString(QLatin1String("0.7.3"));

		VerifiedUpdate u;
		u.candidate.release = release;
		u.candidate.asset = asset;
		u.packagePath = QLatin1String(PKG);
		u.size = asset.size;
		u.sha256 = asset.sha256;
		return u;
	}

	InstallationInfo aPortableInstall()
	{
		InstallationInfo info;
		info.kind = InstallPortable;
		info.installDir = QLatin1String(TARGET);
		info.executablePath = QLatin1String(TARGET) + QLatin1String("/Contents/MacOS/PE-bear");
		info.writable = true;
		return info;
	}

	FakeArchiveReader makeAppBundlePackage()
	{
		FakeArchiveReader archive;
		archive.addDir(QLatin1String("PE-bear.app"));
		archive.addDir(QLatin1String("PE-bear.app/Contents"));
		archive.addDir(QLatin1String("PE-bear.app/Contents/MacOS"));
		archive.addFile(QLatin1String("PE-bear.app/Contents/MacOS/PE-bear"), QByteArray("mac_binary"));
		archive.addFile(QLatin1String("PE-bear.app/Contents/Info.plist"), QByteArray("plist"));
		return archive;
	}

	FakeArchiveReader makeBareExecutablePackage()
	{
		FakeArchiveReader archive;
		archive.addFile(QLatin1String("PE-bear"), QByteArray("mac_binary"));
		archive.addFile(QLatin1String("readme.txt"), QByteArray("readme"));
		return archive;
	}

}; // namespace

class TestMacOSInstaller : public QObject
{
	Q_OBJECT

private slots:
	void init() {}
	void cleanup() {}

	void macOSInstallerName();
	void canInstallWritesToWritableDirectory();
	void canInstallRefusesNonWritableDirectory();
	void prepareStagingExtractsAppBundle();
	void prepareStagingExtractsBareExecutable();
	void prepareStagingHandlesExtractionFailure();
	void verifyStagedLayoutAcceptsValidAppBundle();
	void verifyStagedLayoutAcceptsValidBareExecutable();
	void verifyStagedLayoutRefusesInvalidAppBundle();
	void executablePathInFindsAppBundle();
	void executablePathInFindsBareExecutable();
	void activateMovesAppBundleIntoPlace();
	void activateFailsWhenTargetExists();
};

void TestMacOSInstaller::macOSInstallerName()
{
	FakeFileSystem fs;
	FakeArchiveReader reader;
	MacOSInstaller installer(&fs, &reader);
	
	QCOMPARE(installer.name(), QLatin1String("macos"));
}

void TestMacOSInstaller::canInstallWritesToWritableDirectory()
{
	FakeFileSystem fs;
	FakeArchiveReader reader;
	MacOSInstaller installer(&fs, &reader);
	
	InstallationInfo info = aPortableInstall();
	QString reason;
	
	QVERIFY(installer.canInstall(info, &reason));
	QVERIFY(reason.isEmpty());
}

void TestMacOSInstaller::canInstallRefusesNonWritableDirectory()
{
	FakeFileSystem fs;
	FakeArchiveReader reader;
	MacOSInstaller installer(&fs, &reader);
	
	InstallationInfo info = aPortableInstall();
	info.writable = false;
	QString reason;
	
	QVERIFY(!installer.canInstall(info, &reason));
	QVERIFY(!reason.isEmpty());
}

void TestMacOSInstaller::prepareStagingExtractsAppBundle()
{
	FakeFileSystem fs;
	FakeArchiveReader reader = makeAppBundlePackage();
	MacOSInstaller installer(&fs, &reader);
	
	VerifiedUpdate update = aVerifiedUpdate();
	QList<TransactionOp> ops;
	
	fs.addDir(QLatin1String(PKG));
	
	QVERIFY(installer.prepareStaging(update, QLatin1String(STAGING), &ops));
	
	/* Check that the staged root was found */
	QVERIFY(!installer.stagedRoot().isEmpty());
	
	/* Check that bundle files were created */
	QVERIFY(fs.exists(QLatin1String(STAGING "/PE-bear.app/Contents/MacOS/PE-bear")));
	QVERIFY(fs.exists(QLatin1String(STAGING "/PE-bear.app/Contents/Info.plist")));
}

void TestMacOSInstaller::prepareStagingExtractsBareExecutable()
{
	FakeFileSystem fs;
	FakeArchiveReader reader = makeBareExecutablePackage();
	MacOSInstaller installer(&fs, &reader);
	
	VerifiedUpdate update = aVerifiedUpdate();
	QList<TransactionOp> ops;
	
	fs.addDir(QLatin1String(PKG));
	
	QVERIFY(installer.prepareStaging(update, QLatin1String(STAGING), &ops));
	
	/* Check that the staged root was found */
	QVERIFY(!installer.stagedRoot().isEmpty());
	
	/* Check that files were created */
	QVERIFY(fs.exists(QLatin1String(STAGING "/PE-bear")));
	QVERIFY(fs.exists(QLatin1String(STAGING "/readme.txt")));
}

void TestMacOSInstaller::prepareStagingHandlesExtractionFailure()
{
	FakeFileSystem fs;
	FakeArchiveReader reader;
	reader.setOpenFails();
	MacOSInstaller installer(&fs, &reader);
	
	VerifiedUpdate update = aVerifiedUpdate();
	QList<TransactionOp> ops;
	
	QVERIFY(!installer.prepareStaging(update, QLatin1String(STAGING), &ops));
	QVERIFY(installer.stagedRoot().isEmpty());
}

void TestMacOSInstaller::verifyStagedLayoutAcceptsValidAppBundle()
{
	FakeFileSystem fs;
	FakeArchiveReader reader = makeAppBundlePackage();
	MacOSInstaller installer(&fs, &reader);
	
	VerifiedUpdate update = aVerifiedUpdate();
	QList<TransactionOp> ops;
	
	fs.addDir(QLatin1String(PKG));
	installer.prepareStaging(update, QLatin1String(STAGING), &ops);
	
	QString reason;
	QVERIFY(installer.verifyStagedLayout(QLatin1String(STAGING), &reason));
	QVERIFY(reason.isEmpty());
}

void TestMacOSInstaller::verifyStagedLayoutAcceptsValidBareExecutable()
{
	FakeFileSystem fs;
	FakeArchiveReader reader = makeBareExecutablePackage();
	MacOSInstaller installer(&fs, &reader);
	
	VerifiedUpdate update = aVerifiedUpdate();
	QList<TransactionOp> ops;
	
	fs.addDir(QLatin1String(PKG));
	installer.prepareStaging(update, QLatin1String(STAGING), &ops);
	
	QString reason;
	QVERIFY(installer.verifyStagedLayout(QLatin1String(STAGING), &reason));
	QVERIFY(reason.isEmpty());
}

void TestMacOSInstaller::verifyStagedLayoutRefusesInvalidAppBundle()
{
	FakeFileSystem fs;
	FakeArchiveReader reader;
	MacOSInstaller installer(&fs, &reader);
	
	/* Create a staging directory with an incomplete .app bundle */
	fs.addDir(QLatin1String(STAGING));
	fs.addDir(QLatin1String(STAGING "/PE-bear.app"));
	fs.addDir(QLatin1String(STAGING "/PE-bear.app/Contents"));
	/* Missing MacOS directory */
	
	QString reason;
	QVERIFY(!installer.verifyStagedLayout(QLatin1String(STAGING), &reason));
	QVERIFY(!reason.isEmpty());
}

void TestMacOSInstaller::executablePathInFindsAppBundle()
{
	FakeFileSystem fs;
	FakeArchiveReader reader;
	MacOSInstaller installer(&fs, &reader);
	
	/* Create a .app bundle structure */
	fs.addDir(QLatin1String(STAGING));
	fs.addDir(QLatin1String(STAGING "/PE-bear.app"));
	fs.addDir(QLatin1String(STAGING "/PE-bear.app/Contents"));
	fs.addDir(QLatin1String(STAGING "/PE-bear.app/Contents/MacOS"));
	fs.addFile(QLatin1String(STAGING "/PE-bear.app/Contents/MacOS/PE-bear"));
	
	QString exePath = installer.executablePathIn(QLatin1String(STAGING));
	QCOMPARE(exePath, QLatin1String(STAGING "/PE-bear.app/Contents/MacOS/PE-bear"));
}

void TestMacOSInstaller::executablePathInFindsBareExecutable()
{
	FakeFileSystem fs;
	FakeArchiveReader reader;
	MacOSInstaller installer(&fs, &reader);
	
	/* Create a bare executable structure */
	fs.addDir(QLatin1String(STAGING));
	fs.addFile(QLatin1String(STAGING "/PE-bear"));
	
	QString exePath = installer.executablePathIn(QLatin1String(STAGING));
	QCOMPARE(exePath, QLatin1String(STAGING "/PE-bear.app/Contents/MacOS/PE-bear"));
}

void TestMacOSInstaller::activateMovesAppBundleIntoPlace()
{
	FakeFileSystem fs;
	FakeArchiveReader reader = makeAppBundlePackage();
	MacOSInstaller installer(&fs, &reader);
	
	VerifiedUpdate update = aVerifiedUpdate();
	QList<TransactionOp> ops;
	
	fs.addDir(QLatin1String(PKG));
	installer.prepareStaging(update, QLatin1String(STAGING), &ops);
	
	/* Ensure target doesn't exist */
	QVERIFY(!fs.exists(QLatin1String(TARGET)));
	
	QVERIFY(installer.activate(QLatin1String(STAGING), QLatin1String(TARGET), &ops));
	
	/* Check that the bundle was moved */
	QVERIFY(fs.exists(QLatin1String(TARGET "/PE-bear.app/Contents/MacOS/PE-bear")));
	QVERIFY(fs.exists(QLatin1String(TARGET "/PE-bear.app/Contents/Info.plist")));
}

void TestMacOSInstaller::activateFailsWhenTargetExists()
{
	FakeFileSystem fs;
	FakeArchiveReader reader = makeAppBundlePackage();
	MacOSInstaller installer(&fs, &reader);
	
	VerifiedUpdate update = aVerifiedUpdate();
	QList<TransactionOp> ops;
	
	fs.addDir(QLatin1String(PKG));
	fs.addDir(QLatin1String(TARGET));
	installer.prepareStaging(update, QLatin1String(STAGING), &ops);
	
	QVERIFY(!installer.activate(QLatin1String(STAGING), QLatin1String(TARGET), &ops));
}

QTEST_APPLESS_MAIN(TestMacOSInstaller)