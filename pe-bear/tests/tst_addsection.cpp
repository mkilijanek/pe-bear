/*
 * Adding a section (PeHandler::addSection) when it fails halfway.
 *
 * addSection backs up the headers it is about to change and the file size,
 * then lets bearparser grow the image: SizeOfImage is set first, the file
 * is resized after. If a later step fails, the change already made has to
 * be rolled back from that backup -- and the user's earlier edits on the
 * undo stack must stay as they were.
 */
#include <QtTest>

#include "PeFixture.h"

class TestAddSection : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { ExeFactory::init(); }
	void cleanupTestCase() { ExeFactory::destroy(); }

	void aFailedAddSectionLeavesThePeAsItWas();
};

void TestAddSection::aFailedAddSectionLeavesThePeAsItWas()
{
	PeFixture fixture;
	QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
	PeHandler *hndl = fixture.handler();
	PEFile *pe = fixture.pe();

	/* an earlier edit of the user's, still undoable afterwards */
	QVERIFY(hndl->setByte(0x40, 0x41));
	const size_t opsBefore = hndl->modifHndl.countOperations();

	const QByteArray before((const char*)pe->getContent(), int(pe->getRawSize()));
	const bufsize_t imageSizeBefore = pe->getImageSize();
	const size_t sectionsBefore = pe->getSectionsCount();

	/* the virtual part is fine, the raw part cannot be allocated */
	bool failed = false;
	try {
		hndl->addSection("big", bufsize_t(LLONG_MAX), 0x1000);
	} catch (const CustomException &) {
		failed = true;
	}
	QVERIFY(failed);

	QCOMPARE(pe->getImageSize(), imageSizeBefore);
	QCOMPARE(pe->getSectionsCount(), sectionsBefore);
	QCOMPARE(QByteArray((const char*)pe->getContent(), int(pe->getRawSize())), before);
	QCOMPARE(hndl->modifHndl.countOperations(), opsBefore);

	/* and the user's edit is still the one on top of the undo stack */
	hndl->unModify();
	QCOMPARE(pe->getContent()[0x40], BYTE(0));
}

QTEST_MAIN(TestAddSection)
#include "tst_addsection.moc"
