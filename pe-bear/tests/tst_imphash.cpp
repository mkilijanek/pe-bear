/*
 * Covers the import hash (imphash) PE-bear shows among the file hashes.
 *
 * The reference definition is pefile's get_imphash: "lib.func" entries,
 * lowercased, joined with ',' and hashed as UTF-8. The values below are
 * hashlib.md5(<that string>.encode()).hexdigest().
 */
#include <QtTest>
#include "base/threads/CalcThread.h"
#include "MinimalPe.h"

namespace {

	/* minimal_pe::buildWithImports() puts the function name "TestFunc" here */
	const int FUNC_NAME_RAW = 0x262;

	/** Runs the hash thread over the image and returns the imphash it reports. */
	QString impHashOf(QByteArray image)
	{
		ByteBuffer buffer(reinterpret_cast<BYTE*>(image.data()), bufsize_t(image.size()));
		CalcThread thread(&buffer, 0);
		QString impHash;
		QObject::connect(&thread, &CalcThread::gotHash, [&impHash](QString hash, int type) {
			if (type == SupportedHashes::IMP_MD5) impHash = hash;
		});
		thread.start();
		thread.wait();
		return impHash;
	}

} // namespace

class TestImpHash : public QObject
{
	Q_OBJECT

private slots:
	void asciiNames_matchPefile()
	{
		/* md5("test.testfunc") */
		QCOMPARE(impHashOf(minimal_pe::buildWithImports()), QString("043029cdb46e6cb3bd6da99d9379416b"));
	}

	void nonAsciiName_hashesEveryUtf8Byte()
	{
		QByteArray image = minimal_pe::buildWithImports();
		/* "TestFunc" -> "TéstFun": the same 8 bytes, but 7 characters */
		const QByteArray name("T\xC3\xA9stFun");
		QCOMPARE(name.size(), 8);
		memcpy(image.data() + FUNC_NAME_RAW, name.constData(), size_t(name.size()));

		/* md5("test.téstfun".encode()) */
		QCOMPARE(impHashOf(image), QString("4195f4555b2c31a55ea8c7126483be6a"));
	}
};

QTEST_MAIN(TestImpHash)
#include "tst_imphash.moc"
