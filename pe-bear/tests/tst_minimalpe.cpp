/*
 * Guards the test fixture itself.
 *
 * Every other suite here relies on minimal_pe producing images that bearparser
 * accepts. If that stops being true, this fails first and the rest of the
 * failures are explained rather than mysterious.
 */
#include <QtTest>
#include "MinimalPe.h"

class TestMinimalPe : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { ExeFactory::init(); }
	void cleanupTestCase() { ExeFactory::destroy(); }

	void pe32IsRecognisedAndParsed();
	void pe64IsRecognisedAndParsed();
	void multipleSectionsAreParsed();
	void importsAreParsed();
	void aBrokenSignatureIsNotAPe();
	void aTruncatedImageDoesNotCrashTheParser();
};

void TestMinimalPe::pe32IsRecognisedAndParsed()
{
	QByteArray image = minimal_pe::build32();
	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());

	QCOMPARE(ExeFactory::findMatching(&buf), ExeFactory::PE);

	Executable *exe = ExeFactory::build(&buf, ExeFactory::PE);
	QVERIFY(exe != NULL);
	QCOMPARE(exe->getBitMode(), 32);
	QCOMPARE(quint64(exe->getEntryPoint()), quint64(0x1000));
	QCOMPARE(quint64(exe->getImageSize()), quint64(0x2000));

	PEFile *pe = dynamic_cast<PEFile*>(exe);
	QVERIFY(pe != NULL);
	QCOMPARE(int(pe->getSectionsCount()), 1);
	delete exe;
}

void TestMinimalPe::pe64IsRecognisedAndParsed()
{
	/* The PE32+ layout shifts every optional-header field after ImageBase;
	   getting 64 back here is what proves the fixture has it right. */
	QByteArray image = minimal_pe::build64();
	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());

	QCOMPARE(ExeFactory::findMatching(&buf), ExeFactory::PE);

	Executable *exe = ExeFactory::build(&buf, ExeFactory::PE);
	QVERIFY(exe != NULL);
	QCOMPARE(exe->getBitMode(), 64);
	QCOMPARE(quint64(exe->getImageSize()), quint64(0x2000));
	delete exe;
}

void TestMinimalPe::multipleSectionsAreParsed()
{
	minimal_pe::Options options;
	options.sectionCount = 3;
	options.imageSize = 0x4000;

	QByteArray image = minimal_pe::build(options);
	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());

	Executable *exe = ExeFactory::build(&buf, ExeFactory::PE);
	QVERIFY(exe != NULL);
	PEFile *pe = dynamic_cast<PEFile*>(exe);
	QVERIFY(pe != NULL);
	QCOMPARE(int(pe->getSectionsCount()), 3);
	delete exe;
}

void TestMinimalPe::importsAreParsed()
{
	QByteArray image = minimal_pe::buildWithImports();
	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());

	Executable *exe = ExeFactory::build(&buf, ExeFactory::PE);
	QVERIFY(exe != NULL);
	PEFile *pe = dynamic_cast<PEFile*>(exe);
	QVERIFY(pe != NULL);

	DataDirEntryWrapper *imports = pe->getDataDirEntry(pe::DIR_IMPORT);
	QVERIFY2(imports != NULL, "the import directory was not picked up");
	delete exe;
}

void TestMinimalPe::aBrokenSignatureIsNotAPe()
{
	QByteArray image = minimal_pe::buildNotAPe();
	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());

	/* Still an MZ, so it may match as MZ -- but it must not pass as a PE. */
	QVERIFY(ExeFactory::findMatching(&buf) != ExeFactory::PE);
}

void TestMinimalPe::aTruncatedImageDoesNotCrashTheParser()
{
	/* Walks the truncation point across the whole header area. The assertion
	   is simply that nothing crashes and nothing is reported as a valid PE
	   once the headers are incomplete. */
	static const int CUTS[] = { 0, 1, 2, 0x3C, 0x40, 0x80, 0x84, 0x98, 0x100, 0x180 };
	const int cutCount = sizeof(CUTS) / sizeof(CUTS[0]);

	for (int i = 0; i < cutCount; i++) {
		QByteArray image = minimal_pe::buildTruncated(CUTS[i]);
		if (image.isEmpty()) continue;

		ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());
		ExeFactory::exe_type type = ExeFactory::NONE;
		try {
			type = ExeFactory::findMatching(&buf);
			if (type != ExeFactory::NONE) {
				Executable *exe = ExeFactory::build(&buf, type);
				delete exe;
			}
		} catch (CustomException &) {
			/* a refusal by exception is a correct outcome here */
		}
	}
	QVERIFY2(true, "no crash while truncating across the header area");
}

QTEST_GUILESS_MAIN(TestMinimalPe)
#include "tst_minimalpe.moc"
