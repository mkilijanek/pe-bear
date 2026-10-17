/*
 * Adding a function to an imported library (PeHandler::addImportFunc).
 *
 * The new entry claims its slot in the library's First Thunk array, and in
 * the Original First Thunk array when the library has one, so that both
 * stay parallel. The slots are backed up first so one undo reverts them.
 */
#include <QtTest>

#include "PeFixture.h"

class TestImportFunc : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { ExeFactory::init(); }
	void cleanupTestCase() { ExeFactory::destroy(); }

	void addingAFunctionClaimsBothThunkSlots();
};

void TestImportFunc::addingAFunctionClaimsBothThunkSlots()
{
	minimal_pe::Options options;
	options.addImports = true;
	PeFixture fixture(options);
	QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
	PeHandler *hndl = fixture.handler();
	PEFile *pe = fixture.pe();

	/* an unrelated earlier edit of the user's */
	const offset_t userEdit = 0x40;
	QVERIFY(hndl->setByte(userEdit, 0x41));
	const size_t opsBefore = hndl->modifHndl.countOperations();
	const QByteArray headerBefore((const char*)pe->getContent(), 0x80);

	const bool added = hndl->addImportFunc(0);
	QVERIFY(added);

	QCOMPARE(pe->getContent()[userEdit], BYTE(0x41));
	QCOMPARE(QByteArray((const char*)pe->getContent(), 0x80), headerBefore);
	QCOMPARE(hndl->modifHndl.countOperations(), opsBefore + 1);

	/* the new (second) entry holds a placeholder in both arrays: the
	   Original First Thunk at raw 0x244 and the First Thunk at raw 0x254 */
	const DWORD *oft = (const DWORD*)(pe->getContent() + 0x244);
	const DWORD *ft = (const DWORD*)(pe->getContent() + 0x254);
	QCOMPARE(*oft, DWORD(-1));
	QCOMPARE(*ft, DWORD(-1));

	/* and one undo takes both back */
	hndl->unModify();
	QCOMPARE(*oft, DWORD(0));
	QCOMPARE(*ft, DWORD(0));
	QCOMPARE(pe->getContent()[userEdit], BYTE(0x41));
}

QTEST_MAIN(TestImportFunc)
#include "tst_importfunc.moc"
