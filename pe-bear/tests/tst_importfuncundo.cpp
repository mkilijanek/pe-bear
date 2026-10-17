/*
 * A failed "add function" (PeHandler::addImportFunc) and the undo stack.
 *
 * When the new function's thunk slot cannot be reached -- a malformed
 * library whose thunk array points outside the image -- nothing is backed
 * up and nothing is written. The failure path must then not call undo: the
 * operation on top of the stack is the user's own earlier edit.
 */
#include <QtTest>

#include "MinimalPe.h"
#include "base/PeHandler.h"

class TestImportFuncUndo : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { ExeFactory::init(); }
	void cleanupTestCase() { ExeFactory::destroy(); }

	void aFailedAddFunctionUndoesNothingElse();
};

void TestImportFuncUndo::aFailedAddFunctionUndoesNothingElse()
{
	minimal_pe::Options options;
	options.addImports = true;
	QByteArray image = minimal_pe::build(options);

	/* the only library: no Original First Thunk, First Thunk far outside the image */
	const int descriptorRaw = 0x200;
	qToLittleEndian<quint32>(0, (uchar*)image.data() + descriptorRaw + 0x00);
	qToLittleEndian<quint32>(0x7FFF0000, (uchar*)image.data() + descriptorRaw + 0x10);

	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QString path = QDir(dir.path()).absoluteFilePath("broken-thunks.exe");
	{
		QFile out(path);
		QVERIFY(out.open(QIODevice::WriteOnly));
		QCOMPARE(out.write(image), qint64(image.size()));
	}
	/* PeHandler owns the PE and the buffer, and is disposed of with release() */
	FileBuffer *buf = new FileBuffer(path, image.size(), true);
	PEFile *pe = new PEFile(buf);
	PeHandler *hndl = new PeHandler(pe, buf);

	/* the user's earlier edit */
	const offset_t userEdit = 0x40;
	QVERIFY(hndl->setByte(userEdit, 0x41));
	const size_t opsBefore = hndl->modifHndl.countOperations();

	QVERIFY(!hndl->addImportFunc(0));

	QCOMPARE(pe->getContent()[userEdit], BYTE(0x41));
	QCOMPARE(hndl->modifHndl.countOperations(), opsBefore);

	hndl->release();
}

QTEST_MAIN(TestImportFuncUndo)
#include "tst_importfuncundo.moc"
