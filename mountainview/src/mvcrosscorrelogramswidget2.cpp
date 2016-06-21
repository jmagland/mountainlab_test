/******************************************************
** See the accompanying README and LICENSE files
** Author(s): Jeremy Magland
** Created: 3/31/2016
*******************************************************/

#include "mvcrosscorrelogramswidget2.h"
#include "computationthread.h"
#include "histogramview.h"
#include "mvutils.h"
#include "taskprogress.h"
#include "mvmainwindow.h"
#include "tabber.h"

#include <QAction>
#include <QGridLayout>
#include <QList>
#include <QPainter>
#include <math.h>
#include "msmisc.h"
#include "mvmisc.h"

struct Correlogram {
    Correlogram()
    {
        k1 = k2 = 0;
    }

    int k1, k2;
    QList<float> data;
};

class MVCrossCorrelogramsWidget2Computer {
public:
    //input
    QString mlproxy_url;
    DiskReadMda firings;
    MVEventFilter event_filter;
    CrossCorrelogramOptions options;
    int max_dt;

    //output
    QList<Correlogram> correlograms;

    void compute();
};

class MVCrossCorrelogramsWidget2Private {
public:
    MVCrossCorrelogramsWidget2* q;
    MVCrossCorrelogramsWidget2Computer m_computer;
    QList<Correlogram> m_correlograms;

    QGridLayout* m_grid_layout;
    QList<HistogramView*> m_histogram_views;
    int m_num_columns;

    QList<QWidget*> m_child_widgets;

    CrossCorrelogramOptions m_options;

    void do_highlighting();
};

MVCrossCorrelogramsWidget2::MVCrossCorrelogramsWidget2(MVViewAgent* view_agent)
    : MVAbstractView(view_agent)
{
    d = new MVCrossCorrelogramsWidget2Private;
    d->q = this;
    d->m_num_columns = -1;

    QObject::connect(view_agent, SIGNAL(clusterAttributesChanged()), this, SLOT(slot_cluster_attributes_changed()));
    QObject::connect(view_agent, SIGNAL(currentClusterChanged()), this, SLOT(slot_update_highlighting()));
    QObject::connect(view_agent, SIGNAL(selectedClustersChanged()), this, SLOT(slot_update_highlighting()));

    this->recalculateOn(view_agent, SIGNAL(filteredFiringsChanged()));
    this->recalculateOnOptionChanged("cc_max_dt_msec");

    QGridLayout* GL = new QGridLayout;
    GL->setHorizontalSpacing(20);
    GL->setVerticalSpacing(0);
    GL->setMargin(0);
    this->setLayout(GL);
    d->m_grid_layout = GL;

    /*
    d->m_colors["background"] = QColor(240, 240, 240);
    d->m_colors["frame1"] = QColor(245, 245, 245);
    d->m_colors["info_text"] = QColor(80, 80, 80);
    d->m_colors["view_background"] = QColor(245, 245, 245);
    d->m_colors["view_background_highlighted"] = QColor(250, 220, 200);
    d->m_colors["view_background_selected"] = QColor(250, 240, 230);
    d->m_colors["view_background_hovered"] = QColor(240, 245, 240);
    */

    this->setFocusPolicy(Qt::StrongFocus);

    {
        QAction* a = new QAction("Export image", this);
        this->addAction(a);
        connect(a, SIGNAL(triggered(bool)), this, SLOT(slot_export_image()));
    }

    this->recalculate();
}

MVCrossCorrelogramsWidget2::~MVCrossCorrelogramsWidget2()
{
    delete d;
}

void MVCrossCorrelogramsWidget2::prepareCalculation()
{
    d->m_computer.mlproxy_url = viewAgent()->mlProxyUrl();
    d->m_computer.firings = viewAgent()->firings();
    d->m_computer.event_filter = viewAgent()->eventFilter();
    d->m_computer.options = d->m_options;
    d->m_computer.max_dt = viewAgent()->option("cc_max_dt_msec", 100).toDouble() / 1000 * viewAgent()->sampleRate();
}

void MVCrossCorrelogramsWidget2::runCalculation()
{
    d->m_computer.compute();
}

class TimeScaleWidget2 : public QWidget {
public:
    TimeScaleWidget2();
    int m_time_width;

protected:
    void paintEvent(QPaintEvent* evt);
};

float compute_max2(const QList<Correlogram>& data0)
{
    float ret = 0;
    for (int i = 0; i < data0.count(); i++) {
        QList<float> tmp = data0[i].data;
        for (int j = 0; j < tmp.count(); j++) {
            if (tmp[j] > ret)
                ret = tmp[j];
        }
    }
    return ret;
}

void MVCrossCorrelogramsWidget2::onCalculationFinished()
{
    d->m_correlograms = d->m_computer.correlograms;

    qDeleteAll(d->m_histogram_views);
    d->m_histogram_views.clear();

    qDeleteAll(d->m_child_widgets);
    d->m_child_widgets.clear();

    QGridLayout* GL = d->m_grid_layout;
    float sample_freq = viewAgent()->sampleRate();
    float bin_max = compute_max2(d->m_correlograms);
    float bin_min = -bin_max;
    //int num_bins=100;
    int bin_size = 20;
    int num_bins = (bin_max - bin_min) / bin_size;
    if (num_bins < 100)
        num_bins = 100;
    if (num_bins > 2000)
        num_bins = 2000;
    float time_width = (bin_max - bin_min) / sample_freq * 1000;

    int NUM = d->m_correlograms.count();
    int num_rows = (int)sqrt(NUM);
    if (num_rows < 1)
        num_rows = 1;
    int num_cols = (NUM + num_rows - 1) / num_rows;
    d->m_num_columns = num_cols;
    for (int ii = 0; ii < d->m_correlograms.count(); ii++) {
        HistogramView* HV = new HistogramView;
        HV->setData(d->m_correlograms[ii].data);
        HV->setColors(viewAgent()->colors());
        //HV->autoSetBins(50);
        HV->setBins(bin_min, bin_max, num_bins);
        QString title0 = QString("%1/%2").arg(d->m_correlograms[ii].k1).arg(d->m_correlograms[ii].k2);
        HV->setTitle(title0);
        int row0 = (ii) / num_cols;
        int col0 = (ii) % num_cols;
        GL->addWidget(HV, row0, col0);
        HV->setProperty("row", row0);
        HV->setProperty("col", col0);
        HV->setProperty("index", ii);
        connect(HV, SIGNAL(control_clicked()), this, SLOT(slot_histogram_view_control_clicked()));
        connect(HV, SIGNAL(clicked()), this, SLOT(slot_histogram_view_clicked()));
        connect(HV, SIGNAL(activated()), this, SLOT(slot_histogram_view_activated()));
        connect(HV, SIGNAL(signalExportHistogramMatrixImage()), this, SLOT(slot_export_image()));
        d->m_histogram_views << HV;
    }

    TimeScaleWidget2* TSW = new TimeScaleWidget2;
    TSW->m_time_width = time_width;
    GL->addWidget(TSW, num_rows, 0);

    d->m_child_widgets << TSW;
}

void MVCrossCorrelogramsWidget2::setOptions(CrossCorrelogramOptions opts)
{
    d->m_options = opts;
    this->recalculate();
}

bool sets_match2(const QSet<int>& S1, const QSet<int>& S2)
{
    foreach (int a, S1)
        if (!S2.contains(a))
            return false;
    foreach (int a, S2)
        if (!S1.contains(a))
            return false;
    return true;
}

QImage MVCrossCorrelogramsWidget2::renderImage(int W, int H)
{
    if (!W)
        W = 1800;
    if (!H)
        H = 900;
    int max_row = 0, max_col = 0;
    for (int i = 0; i < d->m_histogram_views.count(); i++) {
        HistogramView* HV = d->m_histogram_views[i];
        int row = HV->property("row").toInt();
        int col = HV->property("col").toInt();
        if (row > max_row)
            max_row = row;
        if (col > max_col)
            max_col = col;
    }
    int NR = max_row + 1, NC = max_col + 1;
    int spacingx = 10;
    int spacingy = 10;
    int W0 = (W - spacingx * (NC + 1)) / NC;
    int H0 = (H - spacingy * (NR + 1)) / NR;

    QImage ret = QImage(W, H, QImage::Format_RGB32);
    QPainter painter(&ret);
    painter.fillRect(0, 0, ret.width(), ret.height(), Qt::white);

    for (int i = 0; i < d->m_histogram_views.count(); i++) {
        HistogramView* HV = d->m_histogram_views[i];
        int row = HV->property("row").toInt();
        int col = HV->property("col").toInt();
        QImage img = HV->renderImage(W0, H0);
        int x0 = spacingx + (W0 + spacingx) * col;
        int y0 = spacingy + (H0 + spacingy) * row;
        painter.drawImage(x0, y0, img);
    }

    return ret;
}

void MVCrossCorrelogramsWidget2::paintEvent(QPaintEvent* evt)
{
    QWidget::paintEvent(evt);

    QPainter painter(this);
    if (isCalculating()) {
        //show that something is computing
        painter.fillRect(QRectF(0, 0, width(), height()), viewAgent()->color("calculation-in-progress"));
    }
}

TimeScaleWidget2::TimeScaleWidget2()
{
    setFixedHeight(25);
    m_time_width = 0;
}

void TimeScaleWidget2::paintEvent(QPaintEvent* evt)
{
    Q_UNUSED(evt)
    QPainter painter(this);
    QPen pen = painter.pen();
    pen.setColor(Qt::black);
    painter.setPen(pen);

    int W0 = width();
    //int H0=height();
    int H1 = 8;
    int margin1 = 6;
    int len1 = 6;
    QPointF pt1(W0 / 2, H1);
    QPointF pt2(W0 - margin1, H1);
    QPointF pt3(W0 / 2, H1 - len1);
    QPointF pt4(W0 - margin1, H1 - len1);
    painter.drawLine(pt1, pt2);
    painter.drawLine(pt1, pt3);
    painter.drawLine(pt2, pt4);

    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    QRect text_box(W0 / 2, H1 + 3, W0 / 2, H1 + 3);
    QString txt = QString("%1 ms").arg((int)(m_time_width / 2 + 0.5));
    painter.drawText(text_box, txt, Qt::AlignCenter | Qt::AlignTop);
}

void MVCrossCorrelogramsWidget2::slot_histogram_view_control_clicked()
{
    int index = sender()->property("index").toInt();
    //int k1 = d->m_labels2.value(index);
    int k1 = d->m_correlograms.value(index).k1;
    //int k2 = d->m_correlograms.value(index).k2;

    viewAgent()->clickCluster(k1, Qt::ControlModifier);

    /*
    //int k2 = d->m_labels2.value(index);
    if (d->m_current_index == index) {
        setCurrentIndex(-1);
    }
    if (!d->m_selected_indices.contains(index)) {
        d->m_selected_indices << index;
        d->do_highlighting();
        if (d->m_current_index <= 0)
            setCurrentIndex(index);
    } else {
        d->m_selected_indices.remove(index);
        d->do_highlighting();
    }
    emit selectedIndicesChanged();
    */
}

void MVCrossCorrelogramsWidget2::slot_histogram_view_clicked()
{

    int index = sender()->property("index").toInt();
    //int k1 = d->m_labels2.value(index);
    int k2 = d->m_correlograms.value(index).k2;

    viewAgent()->clickCluster(k2, Qt::NoModifier);

    /*
    int index = sender()->property("index").toInt();
    d->m_selected_indices.clear();
    if (d->m_current_index == index) {
    } else {
        setCurrentIndex(index);
        d->m_selected_indices.clear();
        d->m_selected_indices << index;
        d->do_highlighting();
        emit selectedIndicesChanged();
        update();
    }
    emit selectedIndicesChanged();
    */
}

void MVCrossCorrelogramsWidget2::slot_histogram_view_activated()
{
    emit histogramActivated();
}

void MVCrossCorrelogramsWidget2::slot_export_image()
{
    QImage img = this->renderImage();
    user_save_image(img);
}

void MVCrossCorrelogramsWidget2::slot_cluster_attributes_changed()
{
    // not implemented for now
}

void MVCrossCorrelogramsWidget2::slot_update_highlighting()
{
    d->do_highlighting();
}

QList<float> compute_cc_data(const QList<double>& times1_in, const QList<double>& times2_in, int max_dt, bool exclude_matches)
{
    QList<float> ret;
    QList<double> times1 = times1_in;
    QList<double> times2 = times2_in;
    qSort(times1);
    qSort(times2);

    if ((times1.isEmpty()) || (times2.isEmpty()))
        return ret;

    long i1 = 0;
    for (long i2 = 0; i2 < times2.count(); i2++) {
        while ((i1 + 1 < times1.count()) && (times1[i1] < times2[i2] - max_dt))
            i1++;
        long j1 = i1;
        while ((j1 < times1.count()) && (times1[j1] <= times2[i2] + max_dt)) {
            bool ok = true;
            if ((exclude_matches) && (j1 == i2) && (times1[j1] == times2[i2]))
                ok = false;
            if (ok) {
                ret << times1[j1] - times2[i2];
            }
            j1++;
        }
    }
    return ret;
}

typedef QList<double> DoubleList;
typedef QList<int> IntList;
void MVCrossCorrelogramsWidget2Computer::compute()
{
    TaskProgress task(TaskProgress::Calculate, QString("Cross Correlograms (%1)").arg(options.mode));

    correlograms.clear();

    firings = compute_filtered_firings_locally(firings, event_filter);

    QList<double> times;
    QList<int> labels;
    long L = firings.N2();

    //assemble the times and labels arrays
    task.setProgress(0.2);
    for (int n = 0; n < L; n++) {
        times << firings.value(1, n);
        labels << (int)firings.value(2, n);
    }

    //compute K (the maximum label)
    int K = compute_max(labels);

    //Assemble the correlogram objects depending on mode
    if (options.mode == All_Auto_Correlograms) {
        for (int k = 1; k <= K; k++) {
            Correlogram CC;
            CC.k1 = k;
            CC.k2 = k;
            this->correlograms << CC;
        }
    }
    else if (options.mode == Cross_Correlograms) {
        int k0 = options.ks.value(0);
        for (int k = 1; k <= K; k++) {
            Correlogram CC;
            CC.k1 = k0;
            CC.k2 = k;
            this->correlograms << CC;
        }
    }
    else if (options.mode == Matrix_Of_Cross_Correlograms) {
        for (int i = 0; i < options.ks.count(); i++) {
            for (int j = 0; j < options.ks.count(); j++) {
                Correlogram CC;
                CC.k1 = options.ks[i];
                CC.k2 = options.ks[j];
                this->correlograms << CC;
            }
        }
    }

    //assemble the times organized by k
    QList<DoubleList> the_times;
    for (int k = 0; k <= K; k++) {
        the_times << DoubleList();
    }
    for (long ii = 0; ii < labels.count(); ii++) {
        int k = labels[ii];
        if (k <= the_times.count()) {
            the_times[k] << times[ii];
        }
    }

    //compute the cross-correlograms
    task.setProgress(0.7);
    for (int j = 0; j < correlograms.count(); j++) {
        if (thread_interrupt_requested()) {
            return;
        }
        int k1 = correlograms[j].k1;
        int k2 = correlograms[j].k2;
        correlograms[j].data = compute_cc_data(the_times.value(k1), the_times.value(k2), max_dt, (k1 == k2));
    }
}

void MVCrossCorrelogramsWidget2Private::do_highlighting()
{
    QList<int> selected_clusters = q->viewAgent()->selectedClusters();
    for (int i = 0; i < m_histogram_views.count(); i++) {
        HistogramView* HV = m_histogram_views[i];
        int index = HV->property("index").toInt();
        if (m_correlograms.value(index).k2 == q->viewAgent()->currentCluster()) {
            HV->setCurrent(true);
        }
        else {
            HV->setCurrent(false);
        }
        if (selected_clusters.contains(m_correlograms.value(index).k2)) {
            HV->setSelected(true);
        }
        else {
            HV->setSelected(false);
        }
    }
}

MVAutoCorrelogramsFactory::MVAutoCorrelogramsFactory(QObject *parent)
    : MVAbstractViewFactory(parent)
{

}

QString MVAutoCorrelogramsFactory::id() const
{
    return QStringLiteral("open-auto-correlograms");
}

QString MVAutoCorrelogramsFactory::name() const
{
    return tr("Auto-Correlograms");
}

QString MVAutoCorrelogramsFactory::title() const
{
    return tr("Auto-Correlograms");
}

MVAbstractView *MVAutoCorrelogramsFactory::createView(MVViewAgent *agent, QWidget *parent)
{
    MVCrossCorrelogramsWidget2* X = new MVCrossCorrelogramsWidget2(agent);
    CrossCorrelogramOptions opts;
    opts.mode = All_Auto_Correlograms;
    X->setOptions(opts);
    QObject::connect(X, SIGNAL(histogramActivated()), this, SLOT(slot_auto_correlogram_activated()));
    return X;
}

void MVAutoCorrelogramsFactory::slot_auto_correlogram_activated()
{
    MVAbstractView *view = qobject_cast<MVAbstractView*>(sender());
    if (!view) return;
    MVMainWindow *mw = MVMainWindow::instance();
    int k = mw->viewAgent()->currentCluster();
    TabberTabWidget* TW = mw->tabWidget(view);
    mw->tabber()->setCurrentContainer(TW);
    mw->tabber()->switchCurrentContainer();
    /// TODO: d->open_cross_correlograms(k);
    /// mw->openView("cross-correlograms", k);
}

MVMatrixOfCrossCorrelogramsFactory::MVMatrixOfCrossCorrelogramsFactory(QObject *parent)
    : MVAbstractViewFactory(parent)
{
    connect(MVMainWindow::instance()->viewAgent(), SIGNAL(selectedClustersChanged()),
            this, SLOT(updateEnabled()));
    updateEnabled();
}

QString MVMatrixOfCrossCorrelogramsFactory::id() const
{
    return QStringLiteral("open-matrix-of-cross-correlograms");
}

QString MVMatrixOfCrossCorrelogramsFactory::name() const
{
    return tr("Matrix of Cross-Correlograms");
}

QString MVMatrixOfCrossCorrelogramsFactory::title() const
{
    return tr("CC Matrix");
}

MVAbstractView *MVMatrixOfCrossCorrelogramsFactory::createView(MVViewAgent *agent, QWidget *parent)
{
    MVCrossCorrelogramsWidget2* X = new MVCrossCorrelogramsWidget2(agent);
    QList<int> ks = agent->selectedClusters();
    qSort(ks);
    if (ks.isEmpty())
        return X;
    CrossCorrelogramOptions opts;
    opts.mode = Matrix_Of_Cross_Correlograms;
    opts.ks = ks;
    X->setOptions(opts);
    return X;
}

void MVMatrixOfCrossCorrelogramsFactory::updateEnabled()
{
    setEnabled(!MVMainWindow::instance()->viewAgent()->selectedClusters().isEmpty());
}
