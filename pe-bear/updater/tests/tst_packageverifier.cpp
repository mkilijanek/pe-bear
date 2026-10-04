/*
 * Covers download verification of PR #3 (UT-13, SEC-01, SEC-02, SEC-03).
 *
 * The central rule under test: a package that does not match the published
 * SHA-256 is never left on disk, and nothing is ever acted on before the
 * digest has been confirmed.
 */
#include <QtTest>
#include "../PackageVerifier.h"

using namespace pe_bear::updater;

namespace {

	QString writeTempFile(QTemporaryDir &dir, const QString &name, const QByteArray &content)
	{
		const QString path = QDir(dir.path()).absoluteFilePath(name);
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly)) return QString();
		f.write(content);
		f.close();
		return path;
	}

	QString sha256Of(const QByteArray &content)
	{
		return QString::fromLatin1(
			QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()).toLower();
	}

}; // namespace

class TestPackageVerifier : public QObject
{
	Q_OBJECT

private slots:
	void acceptsAMatchingPackage();
	void hashesLargeFilesInChunks();
	void deletesAPackageWithAWrongDigest();
	void deletesAPackageWithAWrongSize();
	void rejectsAMalformedExpectedDigest();
	void reportsAMissingFile();
	void canKeepAFailingPackageWhenAskedTo();
	void isCaseInsensitiveAboutTheExpectedDigest();

	void asyncVerifierAcceptsAMatchingPackage();
	void asyncVerifierDeletesOnMismatch();
	void asyncVerifierKeepsTheEventLoopAlive();
	void asyncVerifierCanBeCancelled();
};

void TestPackageVerifier::acceptsAMatchingPackage()
{
	QTemporaryDir dir;
	const QByteArray content("PE-bear package contents");
	const QString path = writeTempFile(dir, "pkg.zip", content);

	const PackageVerifier::Result r =
		PackageVerifier::verify(path, content.size(), sha256Of(content));

	QVERIFY(r.ok);
	QCOMPARE(r.error, ErrorNone);
	QCOMPARE(r.sha256, sha256Of(content));
	QCOMPARE(r.size, qint64(content.size()));
	QVERIFY(QFile::exists(path));
}

void TestPackageVerifier::hashesLargeFilesInChunks()
{
	/* Bigger than one chunk, so the streaming path is actually taken. */
	QTemporaryDir dir;
	QByteArray content;
	content.resize(PackageVerifier::CHUNK_BYTES * 3 + 17);
	for (int i = 0; i < content.size(); i++) {
		content[i] = char(i % 251);
	}
	const QString path = writeTempFile(dir, "big.bin", content);

	QCOMPARE(PackageVerifier::computeSha256(path), sha256Of(content));
}

void TestPackageVerifier::deletesAPackageWithAWrongDigest()
{
	QTemporaryDir dir;
	const QByteArray content("tampered contents");
	const QString path = writeTempFile(dir, "pkg.zip", content);
	const QString wrongDigest = sha256Of("something else entirely");

	const PackageVerifier::Result r =
		PackageVerifier::verify(path, content.size(), wrongDigest);

	QVERIFY(!r.ok);
	QCOMPARE(r.error, ErrorDigestMismatch);
	QVERIFY(r.deleted);
	QVERIFY2(!QFile::exists(path), "a package that failed verification was left on disk");
}

void TestPackageVerifier::deletesAPackageWithAWrongSize()
{
	QTemporaryDir dir;
	const QByteArray content("short");
	const QString path = writeTempFile(dir, "pkg.zip", content);

	const PackageVerifier::Result r =
		PackageVerifier::verify(path, 999999, sha256Of(content));

	QVERIFY(!r.ok);
	QCOMPARE(r.error, ErrorSizeMismatch);
	QVERIFY(!QFile::exists(path));
}

void TestPackageVerifier::rejectsAMalformedExpectedDigest()
{
	QTemporaryDir dir;
	const QByteArray content("contents");
	const QString path = writeTempFile(dir, "pkg.zip", content);

	const PackageVerifier::Result r =
		PackageVerifier::verify(path, content.size(), QLatin1String("not-a-digest"));

	QVERIFY(!r.ok);
	QCOMPARE(r.error, ErrorInvalidDigest);
	QVERIFY(!QFile::exists(path));
}

void TestPackageVerifier::reportsAMissingFile()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("absent.zip");

	const PackageVerifier::Result r = PackageVerifier::verify(path, 10, sha256Of("x"));
	QVERIFY(!r.ok);
	QCOMPARE(r.error, ErrorStorage);
}

void TestPackageVerifier::canKeepAFailingPackageWhenAskedTo()
{
	QTemporaryDir dir;
	const QByteArray content("contents");
	const QString path = writeTempFile(dir, "pkg.zip", content);

	const PackageVerifier::Result r =
		PackageVerifier::verify(path, content.size(), sha256Of("other"), false);

	QVERIFY(!r.ok);
	QVERIFY(!r.deleted);
	QVERIFY(QFile::exists(path));
}

void TestPackageVerifier::isCaseInsensitiveAboutTheExpectedDigest()
{
	QTemporaryDir dir;
	const QByteArray content("contents");
	const QString path = writeTempFile(dir, "pkg.zip", content);

	const PackageVerifier::Result r =
		PackageVerifier::verify(path, content.size(), sha256Of(content).toUpper());
	QVERIFY(r.ok);
}

void TestPackageVerifier::asyncVerifierAcceptsAMatchingPackage()
{
	QTemporaryDir dir;
	QByteArray content;
	content.resize(PackageVerifier::CHUNK_BYTES * 5);
	content.fill('A');
	const QString path = writeTempFile(dir, "pkg.zip", content);

	AsyncPackageVerifier verifier;
	QSignalSpy finished(&verifier, SIGNAL(finished(pe_bear::updater::PackageVerifier::Result)));
	verifier.start(path, content.size(), sha256Of(content));
	QVERIFY(finished.wait(5000));

	const PackageVerifier::Result r =
		finished.first().first().value<PackageVerifier::Result>();
	QVERIFY(r.ok);
	QCOMPARE(r.sha256, sha256Of(content));
	QVERIFY(QFile::exists(path));
}

void TestPackageVerifier::asyncVerifierDeletesOnMismatch()
{
	QTemporaryDir dir;
	QByteArray content;
	content.resize(PackageVerifier::CHUNK_BYTES * 2);
	content.fill('B');
	const QString path = writeTempFile(dir, "pkg.zip", content);

	AsyncPackageVerifier verifier;
	QSignalSpy finished(&verifier, SIGNAL(finished(pe_bear::updater::PackageVerifier::Result)));
	verifier.start(path, content.size(), sha256Of("different"));
	QVERIFY(finished.wait(5000));

	const PackageVerifier::Result r =
		finished.first().first().value<PackageVerifier::Result>();
	QVERIFY(!r.ok);
	QCOMPARE(r.error, ErrorDigestMismatch);
	QVERIFY(!QFile::exists(path));
}

void TestPackageVerifier::asyncVerifierKeepsTheEventLoopAlive()
{
	/* The point of the asynchronous verifier: other timers keep firing while
	   a large package is being hashed, so the window stays responsive. */
	QTemporaryDir dir;
	QByteArray content;
	content.resize(PackageVerifier::CHUNK_BYTES * 200);
	content.fill('C');
	const QString path = writeTempFile(dir, "big.bin", content);

	int ticks = 0;
	QTimer ticker;
	ticker.setInterval(1);
	connect(&ticker, &QTimer::timeout, [&ticks]() { ticks++; });
	ticker.start();

	AsyncPackageVerifier verifier;
	QSignalSpy finished(&verifier, SIGNAL(finished(pe_bear::updater::PackageVerifier::Result)));
	verifier.start(path, content.size(), sha256Of(content));
	QVERIFY(finished.wait(10000));
	ticker.stop();

	QVERIFY2(ticks > 0, "the event loop was blocked while hashing");
}

void TestPackageVerifier::asyncVerifierCanBeCancelled()
{
	QTemporaryDir dir;
	QByteArray content;
	content.resize(PackageVerifier::CHUNK_BYTES * 400);
	content.fill('D');
	const QString path = writeTempFile(dir, "big.bin", content);

	AsyncPackageVerifier verifier;
	QSignalSpy finished(&verifier, SIGNAL(finished(pe_bear::updater::PackageVerifier::Result)));
	verifier.start(path, content.size(), sha256Of(content));
	QVERIFY(verifier.isBusy());
	verifier.cancel();

	QCOMPARE(finished.count(), 1);
	const PackageVerifier::Result r =
		finished.first().first().value<PackageVerifier::Result>();
	QVERIFY(!r.ok);
	QCOMPARE(r.error, ErrorCancelled);
	/* Cancelling is not a verification failure, so the file stays. */
	QVERIFY(QFile::exists(path));
}

QTEST_MAIN(TestPackageVerifier)
#include "tst_packageverifier.moc"
