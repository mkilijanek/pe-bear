/*
 * The imports wizard (PeHandler::autoAddImports) and the names it is given.
 *
 * The wizard reserves the space for each DLL name before writing it. A name
 * is written as UTF-8, so a size taken in characters is too small for any
 * name outside ASCII: the name then runs into the thunks placed right after
 * it, and the bytes it overwrites are not in the undo record. Loader DLL
 * names are ASCII anyway, so the wizard refuses anything else up front.
 */
#include <QtTest>

#include "PeFixture.h"
#include "base/ImportsAutoadderSettings.h"

class TestImportWizard : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void aNonAsciiDllNameIsRefusedBeforeAnyChange();
	void namesAreSizedInBytes();
};

void TestImportWizard::initTestCase()
{
	ExeFactory::init();
}

void TestImportWizard::cleanupTestCase()
{
	ExeFactory::destroy();
}

void TestImportWizard::aNonAsciiDllNameIsRefusedBeforeAnyChange()
{
	minimal_pe::Options options;
	options.addImports = true;
	PeFixture fixture(options);
	QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
	PeHandler *hndl = fixture.handler();
	PEFile *pe = fixture.pe();

	const QByteArray before((const char*)pe->getContent(), int(pe->getRawSize()));
	const size_t opsBefore = hndl->modifHndl.countOperations();

	ImportsAutoadderSettings settings;
	settings.addNewSec = true;
	/* 8 characters, 16 bytes in UTF-8 */
	QVERIFY(settings.addImport(QString::fromUtf8("\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87"), "Func"));

	bool refused = false;
	try {
		hndl->autoAddImports(settings);
	} catch (const CustomException &) {
		refused = true;
	}
	QVERIFY2(refused, "a DLL name outside ASCII was accepted");

	QCOMPARE(hndl->modifHndl.countOperations(), opsBefore);
	QCOMPARE(QByteArray((const char*)pe->getContent(), int(pe->getRawSize())), before);
}

void TestImportWizard::namesAreSizedInBytes()
{
	ImportsAutoadderSettings settings;
	QVERIFY(settings.addImport("a.dll", "f"));
	QCOMPARE(int(settings.calcDllNamesSpace()), 6);

	ImportsAutoadderSettings wide;
	QVERIFY(wide.addImport(QString::fromUtf8("\xc5\xbc.dll"), "f"));
	QCOMPARE(int(wide.calcDllNamesSpace()), 7);
}

QTEST_MAIN(TestImportWizard)
#include "tst_importwizard.moc"
