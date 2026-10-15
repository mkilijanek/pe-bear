/*
 * RealFileSystem::writeFile on the real filesystem.
 *
 * writeFile is what the transaction journal is written with, and the journal
 * is read back after an interruption -- by a later helper, or by PE-bear at
 * start. A record that is briefly absent while it is being rewritten is,
 * to that reader, a record that does not exist: a moved-aside installation
 * with nothing that says how to put it back.
 */
#include <QtTest>
#include <QThread>
#include <QAtomicInt>
#include "../FileSystem.h"

#if defined(Q_OS_UNIX)
	#include <unistd.h>
#endif

using namespace pe_bear::updater;

namespace {

	/** Rewrites one file over and over until told to stop. */
	class Rewriter : public QThread
	{
	public:
		Rewriter(const QString &path, int rounds)
			: m_path(path), m_rounds(rounds), m_failures(0) {}

		int failures() const { return m_failures; }
		QString firstError() const { return m_firstError; }

	protected:
		virtual void run()
		{
			RealFileSystem fs;
			for (int i = 0; i < m_rounds; i++) {
				if (!fs.writeFile(m_path, QByteArray("{\"round\": ") + QByteArray::number(i) + "}")) {
					if (m_failures++ == 0) m_firstError = fs.lastError();
				}
			}
		}

	private:
		QString m_path;
		int m_rounds;
		int m_failures;
		QString m_firstError;
	};

}; // namespace

class TestFileSystem : public QObject
{
	Q_OBJECT

private slots:
	void writeFileReplacesTheContent();
	void writeFileLeavesNothingBesideTheFile();
	void aReaderNeverFindsTheFileMissingWhileItIsRewritten();
	void anOwnerOnlyDirectoryIsPrivate();
	void aLinkIsNotPrivateWhatEverItPointsAt();
	void aDirectoryOthersCanWriteIsNotPrivate();
	void aDirectoryOfAnotherUserIsNotPrivate();
	void restrictToOwnerDoesNotFollowALink();
};

void TestFileSystem::writeFileReplacesTheContent()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QLatin1String("record.json"));

	RealFileSystem fs;
	QVERIFY(fs.writeFile(path, QByteArray("first")));
	QVERIFY(fs.writeFile(path, QByteArray("second")));
	QCOMPARE(fs.readFile(path), QByteArray("second"));
}

void TestFileSystem::writeFileLeavesNothingBesideTheFile()
{
	/* The journal lists its directory and takes every *.json as a record;
	   whatever writeFile uses on the way must not linger there. */
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QLatin1String("record.json"));

	RealFileSystem fs;
	for (int i = 0; i < 3; i++) QVERIFY(fs.writeFile(path, QByteArray::number(i)));
	QCOMPARE(fs.listDir(dir.path()), QStringList() << QLatin1String("record.json"));
}

void TestFileSystem::aReaderNeverFindsTheFileMissingWhileItIsRewritten()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QLatin1String("record.json"));

	RealFileSystem fs;
	QVERIFY(fs.writeFile(path, QByteArray("{\"round\": -1}")));

	Rewriter writer(path, 2000);
	writer.start();

	int missing = 0;
	int checks = 0;
	while (!writer.isFinished()) {
		checks++;
		if (!QFileInfo::exists(path)) missing++;
	}
	writer.wait();

	QVERIFY2(writer.failures() == 0, qPrintable(QString::number(writer.failures())
		+ QLatin1String(" of 2000 writes failed, the first with: ") + writer.firstError()));
	QVERIFY2(missing == 0, qPrintable(QString::number(missing) + QLatin1String(" of ")
		+ QString::number(checks) + QLatin1String(" checks found no file")));
	QVERIFY(fs.readFile(path).startsWith("{\"round\": 1999}"));
}

void TestFileSystem::anOwnerOnlyDirectoryIsPrivate()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	RealFileSystem fs;
	const QString dir = tmp.path() + QLatin1String("/staging");
	QVERIFY(fs.makeDir(dir));
	QVERIFY(fs.restrictToOwner(dir));
	QString why;
	QVERIFY2(fs.isPrivateDirectory(dir, &why), qPrintable(why));
}

void TestFileSystem::aLinkIsNotPrivateWhatEverItPointsAt()
{
#if defined(Q_OS_UNIX)
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	RealFileSystem fs;
	const QString target = tmp.path() + QLatin1String("/elsewhere");
	QVERIFY(fs.makeDir(target));
	QVERIFY(fs.restrictToOwner(target));
	const QString link = tmp.path() + QLatin1String("/.PE-bear-staging");
	QVERIFY(QFile::link(target, link));

	/* the target is ours and private; the link is what is being asked about */
	QString why;
	QVERIFY(!fs.isPrivateDirectory(link, &why));
	QVERIFY2(why.contains(QLatin1String("link")), qPrintable(why));
#else
	QSKIP("directory links are checked on POSIX hosts here");
#endif
}

void TestFileSystem::aDirectoryOthersCanWriteIsNotPrivate()
{
#if defined(Q_OS_UNIX)
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	RealFileSystem fs;
	const QString dir = tmp.path() + QLatin1String("/shared");
	QVERIFY(fs.makeDir(dir));
	QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
		| QFile::WriteGroup | QFile::ReadGroup | QFile::ExeGroup));
	QString why;
	QVERIFY(!fs.isPrivateDirectory(dir, &why));
	QVERIFY2(why.contains(QLatin1String("written")), qPrintable(why));

	/* and tightening it, which is what the installer does first, fixes that */
	QVERIFY(fs.restrictToOwner(dir));
	QVERIFY(fs.isPrivateDirectory(dir));
#else
	QSKIP("permission bits are checked on POSIX hosts here");
#endif
}

void TestFileSystem::aDirectoryOfAnotherUserIsNotPrivate()
{
#if defined(Q_OS_UNIX)
	if (::geteuid() != 0) QSKIP("needs root to hand a directory to another user");
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	RealFileSystem fs;
	const QString dir = tmp.path() + QLatin1String("/theirs");
	QVERIFY(fs.makeDir(dir));
	QVERIFY(fs.restrictToOwner(dir));
	QVERIFY(fs.isPrivateDirectory(dir));
	/* 4242: a uid that is not ours and not root's */
	if (::chown(QFile::encodeName(dir).constData(), 4242, static_cast<gid_t>(-1)) != 0) {
		QSKIP("could not change the owner here");
	}
	QString why;
	QVERIFY(!fs.isPrivateDirectory(dir, &why));
	QVERIFY2(why.contains(QLatin1String("another user")), qPrintable(why));
#else
	QSKIP("ownership is checked on POSIX hosts here");
#endif
}

void TestFileSystem::restrictToOwnerDoesNotFollowALink()
{
#if defined(Q_OS_UNIX)
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	RealFileSystem fs;
	const QString target = tmp.path() + QLatin1String("/public");
	QVERIFY(fs.makeDir(target));
	const QFile::Permissions open = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
		| QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;
	QVERIFY(QFile::setPermissions(target, open));
	const QString link = tmp.path() + QLatin1String("/link");
	QVERIFY(QFile::link(target, link));

	QVERIFY(!fs.restrictToOwner(link));
	/* whatever the link pointed at keeps its mode */
	QCOMPARE(QFileInfo(target).permissions() & open, open);
#else
	QSKIP("directory links are checked on POSIX hosts here");
#endif
}

QTEST_GUILESS_MAIN(TestFileSystem)
#include "tst_filesystem.moc"
