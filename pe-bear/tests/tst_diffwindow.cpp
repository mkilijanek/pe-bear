/*
 * The Compare window and edits of the PEs it shows.
 *
 * DiffWindow is not modal: it stays open while the user edits the PEs it
 * compares. It hands raw pointers into the PE buffers to its hex models, and
 * an edit that resizes a PE (resize, add section, add imports) reallocates
 * that buffer. The window has to give its models the new pointer after every
 * modification, or they read freed memory on the next repaint.
 */
#include <QtTest>

#include "PeFixture.h"
#include "base/MainSettings.h"
#include "base/PeHandlersManager.h"
#include "gui/windows/DiffWindow.h"

class TestDiffWindow : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void contentIsFetchedAgainAfterAResize();

private:
	MainSettings *m_settings;
};

void TestDiffWindow::initTestCase()
{
	ExeFactory::init();
	m_settings = new MainSettings();
	MainSettingsHolder::setMainSettings(m_settings);
}

void TestDiffWindow::cleanupTestCase()
{
	MainSettingsHolder::setMainSettings(NULL);
	delete m_settings;
	m_settings = NULL;
	ExeFactory::destroy();
}

void TestDiffWindow::contentIsFetchedAgainAfterAResize()
{
	PeFixture fixture;
	QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
	PeHandler *hndl = fixture.handler();
	PEFile *pe = fixture.pe();

	BYTE *shown[CNTR] = { NULL, NULL };
	int shownSize[CNTR] = { 0, 0 };
	{
		PeHandlersManager manager;
		DiffWindow window(manager, NULL);
		connect(&window, &DiffWindow::contentChanged,
			[&](BYTE* ptr, int size, offset_t, ContentIndx indx) {
				if (indx >= CNTR) return;
				shown[indx] = ptr;
				shownSize[indx] = size;
			});

		/* the manager releases its handlers on destruction; the fixture owns this one */
		hndl->incRefCntr();
		QVERIFY(manager.insertHandler(hndl));

		/* both sides show the only PE loaded */
		QCOMPARE(shown[LEFT], pe->getContent());
		QCOMPARE(shown[RIGHT], pe->getContent());

		/* an edit in place keeps the buffer, so the views are left as they are */
		shown[LEFT] = shown[RIGHT] = NULL;
		QVERIFY(hndl->setByte(0x40, 0x41));
		QVERIFY(shown[LEFT] == NULL);
		QVERIFY(shown[RIGHT] == NULL);

		/* grown far enough that the buffer cannot stay where it was */
		const bufsize_t newSize = pe->getRawSize() + 0x200000;
		QVERIFY(hndl->resize(newSize));
		QCOMPARE(bufsize_t(pe->getRawSize()), newSize);

		QCOMPARE(shown[LEFT], pe->getContent());
		QCOMPARE(shown[RIGHT], pe->getContent());
		QCOMPARE(shownSize[LEFT], int(newSize));
		QCOMPARE(shownSize[RIGHT], int(newSize));
	}
}

QTEST_MAIN(TestDiffWindow)
#include "tst_diffwindow.moc"
