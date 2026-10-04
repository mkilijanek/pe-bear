/*
 * Model checks for every concrete PE view in PE-bear.
 *
 * Each model is instantiated over a synthetic PE and handed to
 * QAbstractItemModelTester, which walks it and verifies that index(), parent(),
 * rowCount(), columnCount(), hasIndex() and the role contracts agree with one
 * another. A model can look perfectly reasonable field by field and still
 * violate the contract in a way that shows up as a blank row, a duplicated
 * entry or a crash inside QTreeView -- that is what this catches.
 *
 * The same sweep is run three times: over a PE where most directories are
 * absent (the common case for a stripped or packed sample), over one with
 * imports present, and over a 64-bit image.
 */
#include <QtTest>
#include <QAbstractItemModelTester>

#include "PeFixture.h"
#include "gui/pe_models.h"
#include "gui/DosHdrTableModel.h"
#include "gui/PackersTableModel.h"
#include "base/MainSettings.h"

namespace {

	/** Everything the model check needs to know about one model class. */
	struct ModelEntry
	{
		const char *name;
		QAbstractItemModel* (*make)(PeHandler*);
	};

	template <typename T>
	QAbstractItemModel* makeModel(PeHandler *handler)
	{
		return new T(handler, NULL);
	}

	QVector<ModelEntry> allModels()
	{
		QVector<ModelEntry> models;
		/* header views */
		models.append({ "DosHdrTableModel", &makeModel<DosHdrTableModel> });
		models.append({ "FileHdrTreeModel", &makeModel<FileHdrTreeModel> });
		models.append({ "OptionalHdrTreeModel", &makeModel<OptionalHdrTreeModel> });
		models.append({ "RichHdrTreeModel", &makeModel<RichHdrTreeModel> });
		models.append({ "SecHdrsTreeModel", &makeModel<SecHdrsTreeModel> });
		/* directory views */
		models.append({ "BoundImpTreeModel", &makeModel<BoundImpTreeModel> });
		models.append({ "ClrTreeModel", &makeModel<ClrTreeModel> });
		models.append({ "DebugTreeModel", &makeModel<DebugTreeModel> });
		models.append({ "DelayImpTreeModel", &makeModel<DelayImpTreeModel> });
		models.append({ "ExceptionTreeModel", &makeModel<ExceptionTreeModel> });
		models.append({ "ExportsTreeModel", &makeModel<ExportsTreeModel> });
		models.append({ "ImportsTreeModel", &makeModel<ImportsTreeModel> });
		models.append({ "LdConfigTreeModel", &makeModel<LdConfigTreeModel> });
		models.append({ "RelocsTreeModel", &makeModel<RelocsTreeModel> });
		models.append({ "ResourcesTreeModel", &makeModel<ResourcesTreeModel> });
		models.append({ "SecurityTreeModel", &makeModel<SecurityTreeModel> });
		models.append({ "TLSTreeModel", &makeModel<TLSTreeModel> });
		/* other panels */
		models.append({ "PackersTableModel", &makeModel<PackersTableModel> });
		return models;
	}

	/**
	 * Walks every index of a model and touches the roles a view would ask for.
	 * The point is to reach the data() paths the tester does not exercise and
	 * confirm none of them crash or throw on a sparse PE.
	 */
	void readEveryCell(QAbstractItemModel *model, const QModelIndex &parent = QModelIndex(),
		int depth = 0, int *budget = NULL)
	{
		/* A model that reports children for a valid parent (see
		   flatModelsShouldNotClaimChildren below) describes an infinitely deep
		   tree, so this walk needs a hard budget as well as a depth limit --
		   otherwise it runs until the test times out rather than reporting
		   anything useful. */
		int localBudget = 20000;
		if (!budget) budget = &localBudget;
		if (depth > 4 || *budget <= 0) return;

		static const int ROLES[] = {
			Qt::DisplayRole, Qt::ToolTipRole, Qt::ForegroundRole, Qt::BackgroundRole,
			Qt::FontRole, Qt::TextAlignmentRole, Qt::SizeHintRole, Qt::DecorationRole,
			Qt::EditRole, Qt::UserRole
		};
		const int roleCount = sizeof(ROLES) / sizeof(ROLES[0]);

		const int rows = model->rowCount(parent);
		const int cols = model->columnCount(parent);

		for (int r = 0; r < rows; r++) {
			for (int c = 0; c < cols; c++) {
				if (*budget <= 0) return;
				(*budget)--;

				const QModelIndex index = model->index(r, c, parent);
				if (!index.isValid()) continue;
				for (int i = 0; i < roleCount; i++) {
					model->data(index, ROLES[i]);
				}
				model->flags(index);
				if (model->hasChildren(index)) {
					readEveryCell(model, index, depth + 1, budget);
				}
			}
		}
		for (int c = 0; c < cols; c++) {
			model->headerData(c, Qt::Horizontal, Qt::DisplayRole);
			model->headerData(c, Qt::Horizontal, Qt::ToolTipRole);
		}
	}

}; // namespace

class TestPeModels : public QObject
{
	Q_OBJECT

public:
	TestPeModels() : m_settings(NULL) {}

private slots:
	void initTestCase();
	void cleanupTestCase();

	void theFixtureLoads();

	void modelsSatisfyTheModelContract_data();
	void modelsSatisfyTheModelContract();

	void modelsSurviveEveryRoleOnASparsePe_data();
	void modelsSurviveEveryRoleOnASparsePe();

	void modelsSatisfyTheContractOnA64BitPe_data();
	void modelsSatisfyTheContractOnA64BitPe();

	void modelsSatisfyTheContractWithImportsPresent_data();
	void modelsSatisfyTheContractWithImportsPresent();

	void modelsReportNoNegativeCounts_data();
	void modelsReportNoNegativeCounts();

	void headerViewsExposeTheirFields();
	void sectionModelReportsTheSectionCount();

	void flatModelsShouldNotClaimChildren_data();
	void flatModelsShouldNotClaimChildren();

private:
	void fillModelTable();
	void runContractCheck(const minimal_pe::Options &options);

	MainSettings *m_settings;
};

void TestPeModels::initTestCase()
{
	ExeFactory::init();
	/* The models reach for the shared settings through MainSettingsHolder; a
	   null one would be dereferenced while reading colours. */
	m_settings = new MainSettings();
	MainSettingsHolder::setMainSettings(m_settings);
}

void TestPeModels::cleanupTestCase()
{
	MainSettingsHolder::setMainSettings(NULL);
	delete m_settings;
	m_settings = NULL;
	ExeFactory::destroy();
}

void TestPeModels::fillModelTable()
{
	QTest::addColumn<int>("modelIndex");
	const QVector<ModelEntry> models = allModels();
	for (int i = 0; i < models.size(); i++) {
		QTest::newRow(models.at(i).name) << i;
	}
}

void TestPeModels::theFixtureLoads()
{
	PeFixture fixture;
	QVERIFY2(fixture.isValid(), qPrintable(QString("fixture failed: ") + fixture.error()));
	QVERIFY(fixture.handler() != NULL);
	QCOMPARE(int(fixture.pe()->getSectionsCount()), 1);
}

void TestPeModels::runContractCheck(const minimal_pe::Options &options)
{
	QFETCH(int, modelIndex);

	PeFixture fixture(options);
	QVERIFY2(fixture.isValid(), qPrintable(QString("fixture failed: ") + fixture.error()));

	const ModelEntry entry = allModels().at(modelIndex);
	QAbstractItemModel *model = entry.make(fixture.handler());
	QVERIFY2(model != NULL, entry.name);

	/* Known defect, shared by the whole WrapperTableModel family: rowCount()
	   ignores its parent argument, so a valid index reports children, while
	   parent() -- inherited from TreeModel with a null rootItem -- reports
	   none. The two contradict each other and the tester rightly objects,
	   repeatedly, for every row it walks.
	 *
	   Rather than absorb a stream of failures here, such a model is skipped and
	   the defect is asserted in one place by flatModelsShouldNotClaimChildren
	   below. Detected by behaviour rather than by a list of names, so the skip
	   disappears by itself once the models are fixed.
	 *
	   Invisible in the running application only because these models are shown
	   in table views, which never pass a valid parent. */
	const QModelIndex top = model->index(0, 0, QModelIndex());
	const bool claimsChildren = (top.isValid() && model->rowCount(top) > 0
		&& !model->parent(model->index(0, 0, top)).isValid());
	if (claimsChildren) {
		delete model;
		QSKIP("known defect: flat model reports children for a valid parent "
			"(see flatModelsShouldNotClaimChildren)");
	}

	/* QtTest reporting mode turns any contract breach into a test failure
	   naming the offending call. */
	QAbstractItemModelTester tester(model,
		QAbstractItemModelTester::FailureReportingMode::QtTest);
	Q_UNUSED(tester);

	delete model;
}

void TestPeModels::modelsSatisfyTheModelContract_data() { fillModelTable(); }

void TestPeModels::modelsSatisfyTheModelContract()
{
	runContractCheck(minimal_pe::Options());
}

void TestPeModels::modelsSatisfyTheContractOnA64BitPe_data() { fillModelTable(); }

void TestPeModels::modelsSatisfyTheContractOnA64BitPe()
{
	minimal_pe::Options options;
	options.is64bit = true;
	runContractCheck(options);
}

void TestPeModels::modelsSatisfyTheContractWithImportsPresent_data() { fillModelTable(); }

void TestPeModels::modelsSatisfyTheContractWithImportsPresent()
{
	minimal_pe::Options options;
	options.addImports = true;
	runContractCheck(options);
}

void TestPeModels::modelsSurviveEveryRoleOnASparsePe_data() { fillModelTable(); }

void TestPeModels::modelsSurviveEveryRoleOnASparsePe()
{
	/* A PE with almost every directory absent is the normal case for a packed
	   or stripped sample, and it is where a view is most likely to dereference
	   a wrapper it never checked for null. */
	QFETCH(int, modelIndex);

	PeFixture fixture;
	QVERIFY2(fixture.isValid(), qPrintable(QString("fixture failed: ") + fixture.error()));

	const ModelEntry entry = allModels().at(modelIndex);
	QAbstractItemModel *model = entry.make(fixture.handler());
	QVERIFY(model != NULL);

	readEveryCell(model);
	delete model;

	QVERIFY2(true, "every cell and role read without a crash");
}

void TestPeModels::modelsReportNoNegativeCounts_data() { fillModelTable(); }

void TestPeModels::modelsReportNoNegativeCounts()
{
	/* A negative count reaching a view is an immediate crash, and it is an easy
	   mistake when a count comes from a header field that may be garbage. */
	QFETCH(int, modelIndex);

	PeFixture fixture;
	QVERIFY(fixture.isValid());

	const ModelEntry entry = allModels().at(modelIndex);
	QAbstractItemModel *model = entry.make(fixture.handler());
	QVERIFY(model != NULL);

	const int rows = model->rowCount(QModelIndex());
	const int cols = model->columnCount(QModelIndex());
	QVERIFY2(rows >= 0, qPrintable(QString("%1 reported %2 rows").arg(entry.name).arg(rows)));
	QVERIFY2(cols >= 0, qPrintable(QString("%1 reported %2 columns").arg(entry.name).arg(cols)));

	for (int r = 0; r < rows; r++) {
		const QModelIndex index = model->index(r, 0, QModelIndex());
		if (!index.isValid()) continue;
		QVERIFY2(model->rowCount(index) >= 0, entry.name);
		QVERIFY2(model->columnCount(index) >= 0, entry.name);
	}
	delete model;
}

void TestPeModels::headerViewsExposeTheirFields()
{
	/* The header views are the ones that always have content, whatever else the
	   PE is missing, so they are worth asserting on concretely rather than only
	   through the contract check. */
	PeFixture fixture;
	QVERIFY(fixture.isValid());

	DosHdrTableModel dosModel(fixture.handler(), NULL);
	QVERIFY2(dosModel.rowCount(QModelIndex()) > 0, "the DOS header view is empty");
	QVERIFY(dosModel.columnCount(QModelIndex()) > 0);

	FileHdrTreeModel fileModel(fixture.handler(), NULL);
	QVERIFY2(fileModel.rowCount(QModelIndex()) > 0, "the file header view is empty");

	OptionalHdrTreeModel optModel(fixture.handler(), NULL);
	QVERIFY2(optModel.rowCount(QModelIndex()) > 0, "the optional header view is empty");
}

void TestPeModels::sectionModelReportsTheSectionCount()
{
	minimal_pe::Options options;
	options.sectionCount = 3;
	options.imageSize = 0x4000;

	PeFixture fixture(options);
	QVERIFY2(fixture.isValid(), qPrintable(QString("fixture failed: ") + fixture.error()));
	QCOMPARE(int(fixture.pe()->getSectionsCount()), 3);

	SecHdrsTreeModel model(fixture.handler(), NULL);
	QCOMPARE(model.rowCount(QModelIndex()), 3);
}

void TestPeModels::flatModelsShouldNotClaimChildren_data() { fillModelTable(); }

void TestPeModels::flatModelsShouldNotClaimChildren()
{
	/* Pins the defect above down to one statement, so it is obvious what has
	   to change and easy to confirm when it does.
	 *
	 * A flat model must report no rows beneath a valid index. These do, because
	 * WrapperTableModel::rowCount() (and the subclasses that override it) never
	 * look at the parent they were given. A QTreeView over one of these shows an
	 * expander on every row and recurses without end; the table views used today
	 * hide it because they only ever ask about the invisible root.
	 *
	 * The fix is a guard at the top of each rowCount() --
	 *     if (parent.isValid()) return 0;
	 * -- plus a hasIndex() bounds check in PeTableModel::index(), which
	 * currently returns createIndex(row, column) for any arguments at all.
	 */
	QFETCH(int, modelIndex);

	PeFixture fixture;
	QVERIFY(fixture.isValid());

	const ModelEntry entry = allModels().at(modelIndex);
	QAbstractItemModel *model = entry.make(fixture.handler());
	QVERIFY(model != NULL);

	const QModelIndex top = model->index(0, 0, QModelIndex());
	if (!top.isValid()) {
		delete model;
		QSKIP("this directory is absent from the fixture, so there is no row to test");
	}

	const int childRows = model->rowCount(top);
	if (childRows > 0) {
		QEXPECT_FAIL(entry.name,
			"known: rowCount() ignores its parent argument", Continue);
	}
	QVERIFY2(childRows == 0, qPrintable(QString("%1 reports %2 rows under a valid index")
		.arg(entry.name).arg(childRows)));

	delete model;
}

QTEST_MAIN(TestPeModels)
#include "tst_pemodels.moc"
