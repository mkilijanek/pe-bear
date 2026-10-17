/*
 * Covers HexDiffModel's "next diff" navigation in the Compare window.
 *
 * The model is handed a pointer and a size for each side and must never look
 * past either. The buffers here are views into larger arrays whose bytes beyond
 * the given size differ on purpose: a search that reads past the end finds a
 * "difference" there and moves the view off the end of the content.
 */
#include <QtTest>
#include <vector>
#include "gui/HexDiffModel.h"

namespace {

	const bufsize_t CONTENT_SIZE = 0x40;
	const bufsize_t TAIL_SIZE = 0x40;

	/** Same bytes in [0, CONTENT_SIZE) on both sides, different bytes after that. */
	struct DiffPair
	{
		DiffPair()
			: left(CONTENT_SIZE + TAIL_SIZE, 0x00), right(CONTENT_SIZE + TAIL_SIZE, 0xFF)
		{
			for (bufsize_t i = 0; i < CONTENT_SIZE; i++) {
				left[i] = right[i] = BYTE(i);
			}
		}

		void load(HexDiffModel &model)
		{
			model.setContent(left.data(), int(CONTENT_SIZE), 0, LEFT);
			model.setContent(right.data(), int(CONTENT_SIZE), 0, RIGHT);
		}

		std::vector<BYTE> left;
		std::vector<BYTE> right;
	};

} // namespace

class TestHexDiffModel : public QObject
{
	Q_OBJECT

private slots:
	void getDiffStart_findsFirstDifference()
	{
		BYTE a[] = { 1, 2, 3, 4 };
		BYTE b[] = { 1, 2, 9, 4 };
		QCOMPARE(HexDiffModel::getDiffStart(a, 4, b, 4), bufsize_t(2));
		QCOMPARE(HexDiffModel::getDiffStart(a, 2, b, 2), bufsize_t(HexDiffModel::DIFF_NOT_FOUND));
	}

	void nextDiff_identicalContent_staysPut()
	{
		DiffPair pair;
		HexDiffModel model(LEFT);
		pair.load(model);

		model.onGoToNextDiff();
		QCOMPARE(model.getStartOff(), bufsize_t(0));
	}

	// The search from a non-zero offset must cover only what is left after it,
	// not the full content size counted from the advanced pointer.
	void nextDiff_fromOffset_doesNotSearchPastTheEnd()
	{
		DiffPair pair;
		HexDiffModel model(LEFT);
		pair.load(model);

		const bufsize_t start = 0x10;
		model.setStartingOffset(start);
		model.onGoToNextDiff();
		QCOMPARE(model.getStartOff(), start);
	}

	void nextDiff_fromOffset_findsRealDifference()
	{
		DiffPair pair;
		pair.right[0x30] = 0xAA;
		HexDiffModel model(LEFT);
		pair.load(model);

		model.setStartingOffset(0x10);
		model.onGoToNextDiff();
		QCOMPARE(model.getStartOff(), bufsize_t(0x30));
	}
};

QTEST_MAIN(TestHexDiffModel)
#include "tst_hexdiffmodel.moc"
