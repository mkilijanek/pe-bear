/*
 * Covers the pre-existing TreeItem / TreeModel pair that FileHdrTreeModel,
 * OptionalHdrTreeModel, ClrHdrTreeModel and SecHdrsTreeModel are built on.
 *
 * Two kinds of check:
 *  - the tree bookkeeping in TreeItem (re-parenting, detaching, row indices);
 *  - a QAbstractItemModelTester pass over a populated TreeModel, which verifies
 *    index/parent/rowCount/columnCount agree with each other rather than just
 *    each looking plausible on its own.
 */
#include <QtTest>
#include <QAbstractItemModelTester>
#include "gui/TreeModel.h"

namespace {

	/** A TreeItem that actually carries data, for the data() checks. */
	class DataItem : public TreeItem
	{
	public:
		DataItem(const QList<QVariant> &values, TreeItem *parent = NULL)
			: TreeItem(values, parent) {}
	};

	QList<QVariant> row(const QString &a, const QString &b = QString())
	{
		QList<QVariant> values;
		values << a;
		if (!b.isNull()) values << b;
		return values;
	}

	/**
	 * TreeModel keeps rootItem protected and offers no way to populate it, so
	 * a test subclass builds the tree the production models build in their own
	 * constructors.
	 */
	class PopulatedTreeModel : public TreeModel
	{
	public:
		explicit PopulatedTreeModel(int topLevel, int childrenEach, QObject *parent = NULL)
			: TreeModel(parent)
		{
			rootItem = new DataItem(row("Field", "Value"));
			for (int i = 0; i < topLevel; i++) {
				DataItem *node = new DataItem(
					row(QString("node%1").arg(i), QString("v%1").arg(i)));
				rootItem->appendChild(node);
				for (int j = 0; j < childrenEach; j++) {
					node->appendChild(new DataItem(
						row(QString("leaf%1_%2").arg(i).arg(j), QString("lv%1").arg(j))));
				}
			}
		}

		TreeItem* root() { return rootItem; }
	};

}; // namespace

class TestTreeModel : public QObject
{
	Q_OBJECT

private slots:
	/* --- TreeItem --- */
	void aNewItemHasNoChildren();
	void appendChildSetsTheParentBothWays();
	void appendChildReparentsFromAPreviousParent();
	void detachChildClearsTheParent();
	void detachIgnoresAnItemThatIsNotAChild();
	void detachIgnoresNull();
	void rowIsThePositionAmongSiblings();
	void rowOfAnOrphanIsZero();
	void columnCountFollowsTheStoredData();
	void dataReturnsTheStoredValue();
	void dataRejectsAnOutOfRangeColumn();
	void removeAllChildrenEmptiesTheNode();
	void childRejectsAnOutOfRangeRow();

	/* --- TreeModel --- */
	void anEmptyModelReportsNothing();
	void anEmptyModelYieldsNoIndices();
	void populatedModelReportsItsShape();
	void indexAndParentRoundTrip();
	void indexRejectsOutOfRangeRequests();
	void headerDataComesFromTheRootItem();
	void flagsOfAnInvalidIndexAreNone();

	/* --- the model contract --- */
	void anEmptyModelSatisfiesTheModelContract();
	void aPopulatedModelSatisfiesTheModelContract();
	void aDeepModelSatisfiesTheModelContract();
};

void TestTreeModel::aNewItemHasNoChildren()
{
	TreeItem item;
	QCOMPARE(item.childCount(), 0);
	QCOMPARE(item.row(), 0);
	QVERIFY(item.parentItem() == NULL);
}

void TestTreeModel::appendChildSetsTheParentBothWays()
{
	TreeItem parent;
	TreeItem *child = new TreeItem();
	parent.appendChild(child);

	QCOMPARE(parent.childCount(), 1);
	QCOMPARE(parent.child(0), child);
	QCOMPARE(child->parentItem(), &parent);
	/* also adopted as a QObject child, which is what frees it */
	QCOMPARE(child->parent(), static_cast<QObject*>(&parent));
}

void TestTreeModel::appendChildReparentsFromAPreviousParent()
{
	/* SecHdrsTreeModel and FileHdrTreeModel both move an existing node under a
	   new parent when the PE is reloaded, so this has to leave no trace behind
	   in the old parent. */
	TreeItem first;
	TreeItem second;
	TreeItem *child = new TreeItem();

	first.appendChild(child);
	QCOMPARE(first.childCount(), 1);

	second.appendChild(child);
	QVERIFY2(first.childCount() == 0, "the old parent kept the child");
	QCOMPARE(second.childCount(), 1);
	QCOMPARE(child->parentItem(), &second);
}

void TestTreeModel::detachChildClearsTheParent()
{
	TreeItem parent;
	TreeItem *child = new TreeItem();
	parent.appendChild(child);

	parent.detachChild(child);
	QCOMPARE(parent.childCount(), 0);
	QVERIFY(child->parentItem() == NULL);
	QVERIFY(child->parent() == NULL);

	/* detach does not delete, so the caller still owns it */
	delete child;
}

void TestTreeModel::detachIgnoresAnItemThatIsNotAChild()
{
	TreeItem parent;
	TreeItem *ownChild = new TreeItem();
	parent.appendChild(ownChild);

	TreeItem stranger;
	parent.detachChild(&stranger);
	QCOMPARE(parent.childCount(), 1);
}

void TestTreeModel::detachIgnoresNull()
{
	TreeItem parent;
	parent.detachChild(NULL);
	QCOMPARE(parent.childCount(), 0);

	parent.appendChild(NULL);
	QCOMPARE(parent.childCount(), 0);
}

void TestTreeModel::rowIsThePositionAmongSiblings()
{
	TreeItem parent;
	TreeItem *a = new TreeItem();
	TreeItem *b = new TreeItem();
	TreeItem *c = new TreeItem();
	parent.appendChild(a);
	parent.appendChild(b);
	parent.appendChild(c);

	QCOMPARE(a->row(), 0);
	QCOMPARE(b->row(), 1);
	QCOMPARE(c->row(), 2);

	/* removing the middle one renumbers what follows */
	parent.detachChild(b);
	QCOMPARE(a->row(), 0);
	QCOMPARE(c->row(), 1);
	delete b;
}

void TestTreeModel::rowOfAnOrphanIsZero()
{
	TreeItem orphan;
	QCOMPARE(orphan.row(), 0);
}

void TestTreeModel::columnCountFollowsTheStoredData()
{
	DataItem twoColumns(row("a", "b"));
	QCOMPARE(twoColumns.columnCount(), 2);

	TreeItem noData;
	QCOMPARE(noData.columnCount(), 0);
}

void TestTreeModel::dataReturnsTheStoredValue()
{
	/* The original implementation returned a hardcoded "demo" here, ignoring
	   the values it had been constructed with. */
	DataItem item(row("Machine", "0x014C"));
	QCOMPARE(item.data(0).toString(), QString("Machine"));
	QCOMPARE(item.data(1).toString(), QString("0x014C"));
}

void TestTreeModel::dataRejectsAnOutOfRangeColumn()
{
	DataItem item(row("only one"));
	QVERIFY(!item.data(1).isValid());
	QVERIFY(!item.data(-1).isValid());
}

void TestTreeModel::removeAllChildrenEmptiesTheNode()
{
	TreeItem parent;
	parent.appendChild(new TreeItem());
	parent.appendChild(new TreeItem());
	QCOMPARE(parent.childCount(), 2);

	parent.removeAllChildren();
	QCOMPARE(parent.childCount(), 0);
	QVERIFY(parent.child(0) == NULL);
}

void TestTreeModel::childRejectsAnOutOfRangeRow()
{
	TreeItem parent;
	parent.appendChild(new TreeItem());

	QVERIFY(parent.child(0) != NULL);
	QVERIFY(parent.child(1) == NULL);
	QVERIFY(parent.child(-1) == NULL);
}

void TestTreeModel::anEmptyModelReportsNothing()
{
	/* rootItem is null until a subclass populates it; the model has to survive
	   being queried in that state, because the views do query it. */
	TreeModel model;
	QCOMPARE(model.rowCount(QModelIndex()), 0);
	QCOMPARE(model.columnCount(QModelIndex()), 0);
	QVERIFY(!model.headerData(0, Qt::Horizontal, Qt::DisplayRole).isValid());
}

void TestTreeModel::anEmptyModelYieldsNoIndices()
{
	TreeModel model;
	QVERIFY(!model.index(0, 0, QModelIndex()).isValid());
	QVERIFY(!model.parent(QModelIndex()).isValid());
	QVERIFY(!model.data(QModelIndex(), Qt::DisplayRole).isValid());
}

void TestTreeModel::populatedModelReportsItsShape()
{
	PopulatedTreeModel model(3, 2);

	QCOMPARE(model.rowCount(QModelIndex()), 3);
	QCOMPARE(model.columnCount(QModelIndex()), 2);

	const QModelIndex first = model.index(0, 0, QModelIndex());
	QVERIFY(first.isValid());
	QCOMPARE(model.rowCount(first), 2);
}

void TestTreeModel::indexAndParentRoundTrip()
{
	/* The property the views depend on most: walking down and back up again
	   has to land on the index you started from. */
	PopulatedTreeModel model(3, 2);

	for (int i = 0; i < 3; i++) {
		const QModelIndex node = model.index(i, 0, QModelIndex());
		QVERIFY(node.isValid());
		QVERIFY2(!model.parent(node).isValid(), "a top-level node reported a parent");

		for (int j = 0; j < 2; j++) {
			const QModelIndex leaf = model.index(j, 0, node);
			QVERIFY(leaf.isValid());
			QCOMPARE(model.parent(leaf), node);
			QCOMPARE(leaf.row(), j);
		}
	}
}

void TestTreeModel::indexRejectsOutOfRangeRequests()
{
	PopulatedTreeModel model(2, 1);

	QVERIFY(!model.index(2, 0, QModelIndex()).isValid());
	QVERIFY(!model.index(-1, 0, QModelIndex()).isValid());
	QVERIFY(!model.index(0, 2, QModelIndex()).isValid());
	QVERIFY(!model.index(0, -1, QModelIndex()).isValid());
}

void TestTreeModel::headerDataComesFromTheRootItem()
{
	PopulatedTreeModel model(1, 0);
	QCOMPARE(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(), QString("Field"));
	QCOMPARE(model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(), QString("Value"));

	/* vertical headers are not provided */
	QVERIFY(!model.headerData(0, Qt::Vertical, Qt::DisplayRole).isValid());
}

void TestTreeModel::flagsOfAnInvalidIndexAreNone()
{
	PopulatedTreeModel model(1, 1);
	QCOMPARE(model.flags(QModelIndex()), Qt::NoItemFlags);
	QVERIFY(model.flags(model.index(0, 0, QModelIndex())) != Qt::NoItemFlags);
}

void TestTreeModel::anEmptyModelSatisfiesTheModelContract()
{
	TreeModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	Q_UNUSED(tester);
}

void TestTreeModel::aPopulatedModelSatisfiesTheModelContract()
{
	PopulatedTreeModel model(4, 3);
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	Q_UNUSED(tester);
}

void TestTreeModel::aDeepModelSatisfiesTheModelContract()
{
	PopulatedTreeModel model(2, 0);
	/* add a third level by hand, to exercise parent() beyond one hop */
	TreeItem *node = model.root()->child(0);
	QVERIFY(node != NULL);
	TreeItem *mid = new DataItem(row("mid", "x"));
	node->appendChild(mid);
	mid->appendChild(new DataItem(row("deep", "y")));

	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	Q_UNUSED(tester);
}

QTEST_MAIN(TestTreeModel)
#include "tst_treemodel.moc"
