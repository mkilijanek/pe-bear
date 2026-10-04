/*
 * Covers the pre-existing modification/undo machinery: ModifBackup,
 * ResizeBackup, OperationBackup and ModificationHandler.
 *
 * This is the code standing between a user's edit and their original bytes, so
 * the properties worth pinning down are that an undo restores exactly what was
 * there, that undo order is last-in-first-out, and that a resize is undone as
 * one unit together with the edits grouped with it.
 *
 * Operates on a plain bearparser ByteBuffer -- no PE file, no GUI.
 */
#include <QtTest>
#include "base/Modification.h"

namespace {

	/** A buffer filled with a recognisable ascending pattern. */
	class PatternBuffer
	{
	public:
		explicit PatternBuffer(bufsize_t size)
			: m_buffer(size)
		{
			BYTE *content = m_buffer.getContent();
			for (bufsize_t i = 0; i < size; i++) {
				content[i] = BYTE(i & 0xFF);
			}
		}

		ByteBuffer* buffer() { return &m_buffer; }

		QByteArray snapshot()
		{
			return QByteArray(reinterpret_cast<const char*>(m_buffer.getContent()),
				int(m_buffer.getContentSize()));
		}

		void poke(offset_t offset, const QByteArray &bytes)
		{
			BYTE *content = m_buffer.getContent();
			for (int i = 0; i < bytes.size(); i++) {
				content[offset + offset_t(i)] = BYTE(bytes.at(i));
			}
		}

	private:
		ByteBuffer m_buffer;
	};

}; // namespace

class TestModification : public QObject
{
	Q_OBJECT

private slots:
	void undoRestoresTheOriginalBytes();
	void undoIsLastInFirstOut();
	void undoReportsWhenThereIsNothingToUndo();
	void countsOperationsNotModifications();
	void groupedModificationsUndoTogether();
	void reportsTheLastModifiedOffset();
	void reportsWhetherAnOffsetIsInTheLastModifiedArea();
	void unStoreLastDiscardsWithoutRestoring();
	void resizeIsUndoneAsOneUnit();
	void backupRecordsOffsetAndSize();
	void offsetAffectedCoversTheWholeRange();
};

void TestModification::undoRestoresTheOriginalBytes()
{
	PatternBuffer buf(256);
	const QByteArray original = buf.snapshot();

	ModificationHandler handler(buf.buffer(), NULL);
	QVERIFY(handler.backupModification(0x10, 4, false));
	buf.poke(0x10, QByteArray("\xDE\xAD\xBE\xEF", 4));
	QVERIFY(buf.snapshot() != original);

	QVERIFY(handler.undoLastOperation());
	QCOMPARE(buf.snapshot(), original);
}

void TestModification::undoIsLastInFirstOut()
{
	PatternBuffer buf(256);
	const QByteArray original = buf.snapshot();

	ModificationHandler handler(buf.buffer(), NULL);

	QVERIFY(handler.backupModification(0x10, 2, false));
	buf.poke(0x10, QByteArray("\xAA\xAA", 2));
	const QByteArray afterFirst = buf.snapshot();

	QVERIFY(handler.backupModification(0x20, 2, false));
	buf.poke(0x20, QByteArray("\xBB\xBB", 2));

	/* undoing once must roll back only the second edit */
	QVERIFY(handler.undoLastOperation());
	QCOMPARE(buf.snapshot(), afterFirst);

	QVERIFY(handler.undoLastOperation());
	QCOMPARE(buf.snapshot(), original);
}

void TestModification::undoReportsWhenThereIsNothingToUndo()
{
	PatternBuffer buf(64);
	ModificationHandler handler(buf.buffer(), NULL);

	QVERIFY2(!handler.undoLastOperation(), "undo on an untouched buffer reported success");

	QVERIFY(handler.backupModification(0, 1, false));
	QVERIFY(handler.undoLastOperation());
	QVERIFY2(!handler.undoLastOperation(), "undo past the first operation reported success");
}

void TestModification::countsOperationsNotModifications()
{
	PatternBuffer buf(256);
	ModificationHandler handler(buf.buffer(), NULL);
	QCOMPARE(int(handler.countOperations()), 0);

	handler.backupModification(0x10, 2, false);
	QCOMPARE(int(handler.countOperations()), 1);

	/* continueLastOperation groups this into the previous operation */
	handler.backupModification(0x20, 2, true);
	QCOMPARE(int(handler.countOperations()), 1);

	handler.backupModification(0x30, 2, false);
	QCOMPARE(int(handler.countOperations()), 2);
}

void TestModification::groupedModificationsUndoTogether()
{
	/* What makes a multi-field edit feel like one action to the user. */
	PatternBuffer buf(256);
	const QByteArray original = buf.snapshot();

	ModificationHandler handler(buf.buffer(), NULL);
	QVERIFY(handler.backupModification(0x10, 2, false));
	buf.poke(0x10, QByteArray("\xAA\xAA", 2));
	QVERIFY(handler.backupModification(0x40, 2, true));
	buf.poke(0x40, QByteArray("\xBB\xBB", 2));

	QCOMPARE(int(handler.countOperations()), 1);
	QVERIFY(handler.undoLastOperation());
	QCOMPARE(buf.snapshot(), original);
}

void TestModification::reportsTheLastModifiedOffset()
{
	PatternBuffer buf(256);
	ModificationHandler handler(buf.buffer(), NULL);

	QCOMPARE(handler.getLastModifiedOffset(), offset_t(INVALID_ADDR));

	handler.backupModification(0x30, 4, false);
	QCOMPARE(handler.getLastModifiedOffset(), offset_t(0x30));

	handler.backupModification(0x50, 4, false);
	QCOMPARE(handler.getLastModifiedOffset(), offset_t(0x50));
}

void TestModification::reportsWhetherAnOffsetIsInTheLastModifiedArea()
{
	PatternBuffer buf(256);
	ModificationHandler handler(buf.buffer(), NULL);

	QVERIFY2(!handler.isInLastModifiedArea(0x10), "reported a hit with no operations");

	handler.backupModification(0x10, 4, false);
	QVERIFY(handler.isInLastModifiedArea(0x10));
	QVERIFY(handler.isInLastModifiedArea(0x13));
	QVERIFY2(!handler.isInLastModifiedArea(0x14), "the range is inclusive of its end");
	QVERIFY(!handler.isInLastModifiedArea(0x0F));
}

void TestModification::unStoreLastDiscardsWithoutRestoring()
{
	/* Used when an edit turns out not to have changed anything: the record goes
	   away, but the buffer is deliberately left as it is. */
	PatternBuffer buf(256);
	ModificationHandler handler(buf.buffer(), NULL);

	QVERIFY(handler.backupModification(0x10, 4, false));
	buf.poke(0x10, QByteArray("\xDE\xAD\xBE\xEF", 4));
	const QByteArray modified = buf.snapshot();

	QVERIFY(handler.unStoreLast());
	QCOMPARE(int(handler.countOperations()), 0);
	QCOMPARE(buf.snapshot(), modified);
	QVERIFY2(!handler.undoLastOperation(), "the discarded operation was still undoable");
}

void TestModification::resizeIsUndoneAsOneUnit()
{
	PatternBuffer buf(256);
	ModificationHandler handler(buf.buffer(), NULL);
	const bufsize_t originalSize = buf.buffer()->getContentSize();

	QVERIFY(handler.backupResize(512, false));
	QVERIFY(buf.buffer()->resize(512));
	QCOMPARE(bufsize_t(buf.buffer()->getContentSize()), bufsize_t(512));

	QVERIFY(handler.undoLastOperation());
	QCOMPARE(bufsize_t(buf.buffer()->getContentSize()), originalSize);
}

void TestModification::backupRecordsOffsetAndSize()
{
	PatternBuffer buf(256);
	ModifBackup backup(buf.buffer(), 0x20, 8);

	QCOMPARE(backup.getOffset(), offset_t(0x20));
	QCOMPARE(bufsize_t(backup.getSize()), bufsize_t(8));
}

void TestModification::offsetAffectedCoversTheWholeRange()
{
	PatternBuffer buf(256);
	ModifBackup backup(buf.buffer(), 0x20, 8);

	QVERIFY(!backup.isOffsetAffected(0x1F));
	QVERIFY(backup.isOffsetAffected(0x20));
	QVERIFY(backup.isOffsetAffected(0x27));
	QVERIFY(!backup.isOffsetAffected(0x28));
}

QTEST_GUILESS_MAIN(TestModification)
#include "tst_modification.moc"
