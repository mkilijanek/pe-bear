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

using namespace pe_bear::updater;

namespace {

	/** Rewrites one file over and over until told to stop. */
	class Rewriter : public QThread
	{
	public:
		Rewriter(const QString &path, int rounds)
			: m_path(path), m_rounds(rounds), m_failures(0) {}

		int failures() const { return m_failures; }

	protected:
		virtual void run()
		{
			RealFileSystem fs;
			for (int i = 0; i < m_rounds; i++) {
				if (!fs.writeFile(m_path, QByteArray("{\"round\": ") + QByteArray::number(i) + "}")) {
					m_failures++;
				}
			}
		}

	private:
		QString m_path;
		int m_rounds;
		int m_failures;
	};

}; // namespace

class TestFileSystem : public QObject
{
	Q_OBJECT

private slots:
	void writeFileReplacesTheContent();
	void writeFileLeavesNothingBesideTheFile();
	void aReaderNeverFindsTheFileMissingWhileItIsRewritten();
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

	Rewriter writer(path, 5000);
	writer.start();

	int missing = 0;
	int checks = 0;
	while (!writer.isFinished()) {
		checks++;
		if (!QFileInfo::exists(path)) missing++;
	}
	writer.wait();

	QCOMPARE(writer.failures(), 0);
	QVERIFY2(missing == 0, qPrintable(QString::number(missing) + QLatin1String(" of ")
		+ QString::number(checks) + QLatin1String(" checks found no file")));
	QVERIFY(fs.readFile(path).startsWith("{\"round\": 4999}"));
}

QTEST_GUILESS_MAIN(TestFileSystem)
#include "tst_filesystem.moc"
