#include "DiffWindow.h"
#include <QtGlobal>
#include <bearparser/Util.h>
#include "../../HexView.h"

// Util:

template <typename T_INT>
bool hexListToNumber(const QModelIndexList& list, const bool isLittleEndian, T_INT& number)
{
	int size = list.size();
	if (size > sizeof(T_INT)) {
		return false;
	}
	int startIndex = 0;
	int maxIndex = size;
	int step = 1;
	if (!isLittleEndian) {
		startIndex = (size - 1);
		maxIndex = (-1);
		step = (-1);
	}

	for (int i = startIndex; i != maxIndex; i += step) {
		const QModelIndex& index = list.at(i);

		bool isOk;
		uint8_t tempNum = index.data().toString().toInt(&isOk, 16);
		if (!isOk) break;

		number <<= 8;
		number ^= tempNum;
	}
	return number;
}

//------------------------------------------------------------------------------------

DiffWindow::DiffWindow(PeHandlersManager &peMngr, QWidget *parent)
    : QMainWindow(parent), peManger(peMngr),
    hexDumpModelL(LEFT), hexDumpModelR(RIGHT)
{
	for (int viewIndx = 0; viewIndx < CNTR; viewIndx++) {
		peContent[viewIndx] = NULL;
		peSize[viewIndx] = 0;
	}

	QStringList commands;
	commands << "Raw" << "Relative ($+)";
	addrFmtBox.addItems(commands);
	connect(&addrFmtBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onAddrFormatSelected(int)));
	globalToolBar.addWidget(&addrFmtBox);
	globalToolBar.setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);

	createActions();

	this->setWindowTitle("Compare...");
	this->setMinimumSize( QSize(880, 600) );
	this->mainSplitter.setOrientation(Qt::Horizontal);

	this->setCentralWidget(&this->mainSplitter);
	mainSplitter.setAutoFillBackground(true);
	
	mainSplitter.addWidget(&this->leftSplitter);
	mainSplitter.addWidget(&this->rightSplitter);

	leftSplitter.addWidget(&this->fileCombo[LEFT]);
	leftSplitter.addWidget(&this->treeView[LEFT]);

	leftSplitter.addWidget(&this->fileView[LEFT]);
	leftSplitter.addWidget(&this->numEdit[LEFT]);

	globalToolBar.addAction(setHexView);
	globalToolBar.addAction(nextDiff);

	addToolBar(Qt::BottomToolBarArea, &globalToolBar);

	this->treeView[LEFT].setHeaderHidden(true);
	this->treeView[RIGHT].setHeaderHidden(true);
	
	rightSplitter.addWidget(&this->fileCombo[RIGHT]);
	rightSplitter.addWidget(&this->treeView[RIGHT]);

	rightSplitter.addWidget(&this->fileView[RIGHT]);
	rightSplitter.addWidget(&this->numEdit[RIGHT]);

	leftSplitter.setOrientation(Qt::Vertical);
	rightSplitter.setOrientation(Qt::Vertical);
	
// init models:
	fileView[LEFT].setModel(&hexDumpModelL);
	fileView[RIGHT].setModel(&hexDumpModelR);

	connect(&fileScrollBar[LEFT], SIGNAL(sliderMoved(int)), this, SLOT(onSliderMoved(int)) );
	connect(&fileScrollBar[RIGHT], SIGNAL(sliderMoved(int)), this, SLOT(onSliderMoved(int)) );

	this->setStatusBar(&this->statusBar);

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
	connect( &this->fileCombo[LEFT], SIGNAL(currentTextChanged(const QString &)),
		this, SLOT(file1Selected(const QString &)) );
	connect( &this->fileCombo[RIGHT], SIGNAL(currentTextChanged(const QString &)),
		this, SLOT(file2Selected(const QString &)) );
#else
	connect( &this->fileCombo[LEFT], SIGNAL(activated(const QString &)),
		this, SLOT(file1Selected(const QString &)) );
	connect( &this->fileCombo[RIGHT], SIGNAL(activated(const QString &)),
		this, SLOT(file2Selected(const QString &)) );
#endif

	connect( this, SIGNAL(contentChanged(BYTE*, int, offset_t, ContentIndx)),
		&hexDumpModelL, SLOT(setContent(BYTE*, int, offset_t, ContentIndx)) );
		
	connect( this, SIGNAL(contentChanged(BYTE*, int, offset_t, ContentIndx)),
		&hexDumpModelR, SLOT(setContent(BYTE*, int, offset_t, ContentIndx)) );
		
	connect( this, SIGNAL(contentCleared(ContentIndx)), 
		&hexDumpModelL, SLOT(clearContent(ContentIndx)) );
		
	connect( this, SIGNAL(contentCleared(ContentIndx)), 
		&hexDumpModelR, SLOT(clearContent(ContentIndx)) );

	connect(&this->peManger, SIGNAL(PeListUpdated()), this, SLOT(refresh()));
	//-----
	QPalette p = this->fileView[LEFT].palette();
	QColor bgColor = QColor(HEXDIFF_BG);
	bgColor.setAlpha(160);
	QColor bgAltColor = QColor(HEXDIFF_ALTBG);
	bgAltColor.setAlpha(160);
	p.setColor(QPalette::Base, bgColor); 
	p.setColor(QPalette::AlternateBase, bgAltColor);
	p.setColor(QPalette::WindowText, Qt::black);

	this->fileView[LEFT].setPalette(p);
	this->fileView[RIGHT].setPalette(p);
	this->fileView[LEFT].setAutoFillBackground(true);
	this->fileView[RIGHT].setAutoFillBackground(true);
	
	resizeComponents();
}

void DiffWindow::resizeComponents()
{
	const int iconDim = ViewSettings::getIconDim(QApplication::font());
	const int NUM_EDIT_HEIGHT = iconDim * 3;
	const int MAX_HEIGHT = iconDim * 2;

	for (int viewIndx = 0; viewIndx < CNTR; viewIndx++) {
		this->fileCombo[viewIndx].setMaximumHeight(MAX_HEIGHT);
		this->numEdit[viewIndx].setFixedHeight(NUM_EDIT_HEIGHT);
	}
	globalToolBar.setMaximumHeight(MAX_HEIGHT);
}

void DiffWindow::onSliderMoved(int val)
{
	fileScrollBar[LEFT].setSliderPosition(val);
	fileScrollBar[RIGHT].setSliderPosition(val);
}

void DiffWindow::hexSelectedL()
{
	hexSelected(LEFT);
}

void DiffWindow::hexSelectedR()
{
	hexSelected(RIGHT);
}

void DiffWindow::hexSelected(ContentIndx contentIndx)
{
	QItemSelectionModel *model = this->fileView[contentIndx].selectionModel();
	QModelIndexList list = model->selectedIndexes();
	int row = -1;
	int col = -1;
	int size = list.size();

	int64_t bEndianNum = 0;
	int64_t lEndianNum = 0;
	if (!hexListToNumber(list, true, lEndianNum) || !hexListToNumber(list, false, bEndianNum)) {
		this->numEdit[contentIndx].setText("");
		return;
	}

	//static char buff[0x100] = { 0 };
	QString temp;
	QString str;
	if (size > sizeof(uint32_t)) {
		str.append("QWORD\n");
#if QT_VERSION >= 0x050000
		temp = QString::asprintf("%lld = %llu = %016llX", (long long)lEndianNum, (unsigned long long)lEndianNum, (unsigned long long)lEndianNum);
#else
		temp.sprintf("%lld = %llu = %016llX", (long long)lEndianNum, (unsigned long long)lEndianNum, (unsigned long long)lEndianNum);
#endif
		str.append(tr("LEndian : ") + temp + "\n");
#if QT_VERSION >= 0x050000
		temp = QString::asprintf("%lld = %llu = %016llX", (long long)bEndianNum, (unsigned long long)bEndianNum, (unsigned long long)bEndianNum);
#else
		temp.sprintf("%lld = %llu = %016llX", (long long)bEndianNum, (unsigned long long)bEndianNum, (unsigned long long)bEndianNum);
#endif
		str.append(tr("BEndian : ") + temp + "\n");

	} else if (size > sizeof(uint16_t)) {
		str.append("DWORD\n");
#if QT_VERSION >= 0x050000
		temp = QString::asprintf("%d = %u = %08X", (int32_t) lEndianNum, (uint32_t) lEndianNum, (uint32_t) lEndianNum);
#else
		temp.sprintf("%d = %u = %08X", (int32_t) lEndianNum, (uint32_t) lEndianNum, (uint32_t) lEndianNum);
#endif
		str.append("LEndian : " + temp + "\n");
#if QT_VERSION >= 0x050000
		temp = QString::asprintf("%d = %u = %08X", (int32_t) bEndianNum, (uint32_t) bEndianNum, (uint32_t) bEndianNum);
#else
		temp.sprintf("%d = %u = %08X", (int32_t) bEndianNum, (uint32_t) bEndianNum, (uint32_t) bEndianNum);
#endif
		str.append(tr("BEndian : ") + temp + "\n");

	} else if (size > sizeof(uint8_t)) {
		str.append("WORD\n");
#if QT_VERSION >= 0x050000
		temp = QString::asprintf("%d = %u = %04X", (int16_t) lEndianNum, (uint16_t) lEndianNum, (uint16_t) lEndianNum);
#else
		temp.sprintf("%d = %u = %04X", (int16_t) lEndianNum, (uint16_t) lEndianNum, (uint16_t) lEndianNum);
#endif
		str.append(tr("LEndian : ") + temp + "\n");

#if QT_VERSION >= 0x050000
		temp = QString::asprintf( "%d = %u = %04X", (int16_t) bEndianNum, (uint16_t) bEndianNum, (uint16_t) bEndianNum);
#else
		temp.sprintf( "%d = %u = %04X", (int16_t) bEndianNum, (uint16_t) bEndianNum, (uint16_t) bEndianNum);
#endif
		str.append("BEndian : " + temp + "\n");
	} else {
		str.append("BYTE\n");
		temp = QString::number((int8_t) lEndianNum, 10) + " = " + QString::number((uint8_t) lEndianNum, 10) + " = " + QString::number((uint8_t) lEndianNum, 16).toUpper();
		str.append(tr("LEndian : ") + temp + "\n");
		
		temp = QString::number((int8_t) bEndianNum, 10) + " = " + QString::number((uint8_t) bEndianNum, 10) + " = " + QString::number((uint8_t) bEndianNum, 16).toUpper();
		str.append(tr("BEndian : ")+ temp + "\n");
	}
	this->numEdit[contentIndx].setText(str);
}

void DiffWindow::onAddrFormatSelected(int val)
{
	if (val == 0) {
		hexDumpModelL.setRelativeOffset(false);
		hexDumpModelR.setRelativeOffset(false);
	} else {
		hexDumpModelL.setRelativeOffset(true);
		hexDumpModelR.setRelativeOffset(true);
	}
	if (addrFmtBox.currentIndex() != val) {
		addrFmtBox.setCurrentIndex(val);
	}
}

void DiffWindow::createActions()
{
	setHexView = new QAction("Hex View", this);
	setHexView->setCheckable(true);
	setHexView->setChecked(hexDumpModelL.isHexView());
	connect(setHexView, SIGNAL(triggered(bool)), &hexDumpModelL, SLOT(setHexView(bool)) );
	connect(setHexView, SIGNAL(triggered(bool)), &hexDumpModelR, SLOT(setHexView(bool)) );

	this->nextDiff = new QAction("Next Diff", this);
	connect(nextDiff, SIGNAL(triggered()), this, SLOT(onNextDiff()));
}

void DiffWindow::onNextDiff()
{
	hexDumpModelL.onGoToNextDiff();
	hexDumpModelR.onGoToNextDiff();
}

void DiffWindow::onPeModified()
{
	PeHandler *hndl = qobject_cast<PeHandler*>(sender());

	for (int viewIndx = 0; viewIndx < CNTR; viewIndx++) {
		if (currName[viewIndx].size() == 0) continue;
		PeHandler *shown = this->peManger.getByName(currName[viewIndx]);
		if (!shown || (hndl && shown != hndl)) continue;

		PEFile *pe = shown->getPe();
		if (pe && pe->getContent() == peContent[viewIndx] && pe->getRawSize() == peSize[viewIndx]) {
			continue; // the buffer was neither moved nor resized: the pointers held are still valid
		}
		// the content of the marked item may have moved or changed size: show the whole file again
		contentPtr[viewIndx] = NULL;
		contentSize[viewIndx] = 0;
		contentOffset[viewIndx] = 0;
		setPEContent(currName[viewIndx], 0, ContentIndx(viewIndx));
	}
}

void DiffWindow::destroyActions()
{
	delete setHexView;
	setHexView = NULL;

	delete nextDiff;
	nextDiff = NULL;
}

bool DiffWindow::reselectPrevious(QList<QString> &stringsList, ContentIndx contentIndx)
{
	if (contentIndx == CNTR) return false;
	if (stringsList.size() == 0) return false;

	QString prevName = currName[contentIndx];
	bool isContained = false;

	if (stringsList.contains(prevName)) {
		int foundIndx = stringsList.indexOf(prevName, 0);
		fileCombo[contentIndx].setCurrentIndex(foundIndx);
		isContained = true;
	} else {
		prevName = stringsList[0];
	}

	if (contentIndx == LEFT) {
		file1Selected(prevName);
	}
	else {
		file2Selected(prevName);
	}

	return isContained;
}

void DiffWindow::refresh()
{
	emit contentCleared(CNTR);

	for (int viewIndx = 0; viewIndx < CNTR; viewIndx++) {
		treeView[viewIndx].setModel(NULL);

		fileCombo[viewIndx].clear();
		contentPtr[viewIndx] = NULL;
		contentSize[viewIndx] = 0;
		contentOffset[viewIndx] = 0;
	}

	removeUnusedTreeModels();

	QList<QString> stringsList;
	std::map<PEFile*, PeHandler*> hndlMap = this->peManger.getHandlersMap();
	std::map<PEFile*, PeHandler*>::iterator peItr;
	for (peItr = hndlMap.begin(); peItr != hndlMap.end(); ++peItr) {
		QString name = peItr->second->getFullName();
		stringsList.append(name);

		// edits may reallocate the PE buffer: the pointers held here must be fetched again
		connect(peItr->second, SIGNAL(modified()), this, SLOT(onPeModified()), Qt::UniqueConnection);
		connect(peItr->second, SIGNAL(secHeadersModified()), this, SLOT(onPeModified()), Qt::UniqueConnection);
	}

	fileCombo[LEFT].addItems(stringsList);
	fileCombo[RIGHT].addItems(stringsList);
	
	reselectPrevious(stringsList, LEFT);
	reselectPrevious(stringsList, RIGHT);
}

void DiffWindow::file2Selected(const QString &text)
{
	setTreeModel(treeView[RIGHT], text);
	setPEContent(text, 0, RIGHT);

	connect(treeView[RIGHT].selectionModel(), SIGNAL(currentChanged(const QModelIndex &, const QModelIndex &)), 
		this, SLOT( item2Marked(const QModelIndex &, const QModelIndex &)), Qt::UniqueConnection);

	connect(this->fileView[RIGHT].selectionModel(), SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
		this, SLOT(hexSelectedR()), Qt::UniqueConnection);
}

void DiffWindow::file1Selected(const QString &text)
{
	setTreeModel(treeView[LEFT], text);
	setPEContent(text, 0, LEFT);

	connect(treeView[LEFT].selectionModel(), SIGNAL(currentChanged(const QModelIndex &, const QModelIndex &)),
		this, SLOT( item1Marked(const QModelIndex &, const QModelIndex &)), Qt::UniqueConnection);

	connect(this->fileView[LEFT].selectionModel(), SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
		this, SLOT(hexSelectedL()), Qt::UniqueConnection);
}

void DiffWindow::removeUnusedTreeModels()
{
	std::map<QString, PEFile*>::iterator loadedIter;
	std::map<QString, PEFileTreeModel*>::iterator peIter;
	std::set<QString> toErase;

	for (peIter = peModels.begin(); peIter != peModels.end(); ++peIter) {
		const QString &name = peIter->first;

		if (!this->peManger.getByName(name)) {
			toErase.insert(name);
			continue;
		}
	}
	std::set<QString>::iterator eraseIter;
	for (eraseIter = toErase.begin(); eraseIter != toErase.end(); ++eraseIter) {
		const QString name = *eraseIter;
		PEFileTreeModel* model = peModels[name];
		peModels.erase(name);
		delete model;
	}
}

void DiffWindow::setPEContent(const QString &name, int offset, ContentIndx contentIndx)
{
	if (name.size() == 0) return;

	PeHandler* hndl = this->peManger.getByName(name);
	if (hndl == NULL) {
		return;
	}
	currName[contentIndx] = name;
	PEFile *pe = hndl->getPe();
	if (pe == NULL) {
		return;
	}
	BYTE* content = pe->getContent();
	int size = pe->getRawSize();
	peContent[contentIndx] = content;
	peSize[contentIndx] = pe->getRawSize();
	if (content == NULL)
		emit contentChanged(NULL, 0, 0, contentIndx);
	else
		emit contentChanged(content + offset, size - offset, offset, contentIndx);
}

void DiffWindow::setTreeModel(QTreeView &treeView, const QString &name)
{
	if (name.size() == 0) {
		return;
	}
	PeHandler* hndl = this->peManger.getByName(name);
	if (!hndl) {
		return;
	}
	PEFile *pe = hndl->getPe();
	if (pe == NULL) {
		return;
	}

	PEFileTreeModel *model = peModels[name];
	if (model == NULL) {
		model = new PEFileTreeModel(&treeView);
		model->addHandler(hndl);
		peModels[name] = model;
	}

	treeView.setModel(model);
	treeView.expandAll();
}

void DiffWindow::item1Marked(const QModelIndex & current, const QModelIndex & previous)
{
	itemMarked(current, previous, treeView[LEFT], LEFT);
	emit contentChanged(contentPtr[LEFT], contentSize[LEFT], contentOffset[LEFT], LEFT);

}

void DiffWindow::item2Marked(const QModelIndex & current, const QModelIndex & previous)
{
	itemMarked(current, previous, treeView[RIGHT], RIGHT);
	emit contentChanged(contentPtr[RIGHT], contentSize[RIGHT], contentOffset[RIGHT], RIGHT);
}

void DiffWindow::itemMarked(const QModelIndex & current, const QModelIndex & previous, QTreeView &treeView, ContentIndx contentIndx)
{
	if (contentIndx >= CNTR) return;

	QVariant what = (QVariant) treeView.model()->data(current, Qt::WhatsThisRole);

	PEFileTreeItem *item = static_cast<PEFileTreeItem*>(current.internalPointer());

	if (item->getContent() == NULL) return;

	BYTE* newContent = item->getContent();
	size_t newSize = item->getContentSize();

	if (contentPtr[contentIndx] ==  newContent && contentSize[contentIndx] == newSize ) {
		return; // no changes
	}
	contentPtr[contentIndx] = newContent;
	contentSize[contentIndx] = newSize;
	contentOffset[contentIndx] = item->getContentOffset();

	bufsize_t diff = HexDiffModel::getDiffStart(contentPtr[LEFT], contentSize[LEFT], contentPtr[RIGHT], contentSize[RIGHT]);
	if (diff == HexDiffModel::DIFF_NOT_FOUND) {
		if (contentSize[LEFT] != contentSize[RIGHT]) {
			if (contentSize[LEFT] > contentSize[RIGHT])
				this->statusBar.showMessage(tr("LEFT == RIGHT, till: 0x") + QString::number(contentSize[RIGHT], 16).toUpper() + tr("; LEFT longer."));
			else
				this->statusBar.showMessage(tr("LEFT == RIGHT, till: 0x") + QString::number(contentSize[LEFT], 16).toUpper() + tr("; RIGHT longer."));
		} else
			this->statusBar.showMessage(tr("LEFT == RIGHT"));
	} else
		this->statusBar.showMessage(tr("First difference at: 0x") + QString::number(diff, 16).toUpper());
}

