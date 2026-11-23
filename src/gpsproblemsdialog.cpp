#include "gpsproblemsdialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace
{
QString clock(double seconds)
{
    const int total = int(std::lround(seconds));
    const QString minSec = QString("%1:%2").arg(total / 60 % 60, 2, 10, QChar('0')).arg(total % 60, 2, 10, QChar('0'));
    return total >= 3600 ? QString("%1:%2").arg(total / 3600).arg(minSec) : minSec;
}
}

GpsProblemsDialog::GpsProblemsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("GPS problems"));
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_list = new QTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setHeaderLabels({tr("Time"), tr("Problem"), tr("What it is")});
    m_list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this]()
    {
        updateButtons();
        const QList<QTreeWidgetItem *> picked = m_list->selectedItems();
        if (picked.size() == 1)
            {
                const GpsProblems::Problem &problem = m_problems.at(m_list->indexOfTopLevelItem(picked.first()));
                emit chosen(problem.start, problem.end);
            }
    });

    m_fixButton = new QPushButton(tr("Fix"), this);
    m_fixButton->setToolTip(tr("Take the selected wrong positions out: the track goes straight across"));
    connect(m_fixButton, &QPushButton::clicked, this, [this]() { fix(false); });
    m_fixAllButton = new QPushButton(tr("Fix all"), this);
    connect(m_fixAllButton, &QPushButton::clicked, this, [this]() { fix(true); });
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addButton(m_fixButton, QDialogButtonBox::ActionRole);
    buttons->addButton(m_fixAllButton, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addWidget(m_list, 1);
    layout->addWidget(buttons);
    resize(560, 320);
}

void GpsProblemsDialog::show(const TelemetrySessionPtr &recorded, const QVector<DataEdit> &edits)
{
    m_edits = edits;
    const TelemetrySessionPtr corrected = DataEdits::apply(recorded, edits);
    m_problems = corrected ? GpsProblems::find(*corrected, edits) : QVector<GpsProblems::Problem>();
    m_list->clear();
    for (const GpsProblems::Problem &problem : std::as_const(m_problems))
        {
            QString kind;
            QString what;
            const QString meters = QString::number(std::lround(problem.meters));
            switch (problem.kind)
                {
                case GpsProblems::Problem::Gap:
                    kind = tr("Gap");
                    what = tr("No position for %1 s over %2 m: the track goes straight across")
                           .arg(std::lround(problem.end - problem.start)).arg(meters);
                    break;
                case GpsProblems::Problem::Jump:
                    kind = tr("Jump");
                    what = tr("Positions up to %1 m off the way").arg(meters);
                    break;
                case GpsProblems::Problem::Frozen:
                    kind = tr("Frozen");
                    what = tr("The position stood still for %1 s while the speed sensor went on")
                           .arg(std::lround(problem.end - problem.start));
                    break;
                }
            const QString time = problem.end > problem.start ? QString("%1 - %2").arg(clock(problem.start), clock(problem.end))
                                 : clock(problem.start);
            QTreeWidgetItem *item = new QTreeWidgetItem(m_list, {time, kind, what});
            item->setToolTip(2, what);
        }
    m_list->resizeColumnToContents(0);
    m_list->resizeColumnToContents(1);
    const int fixable = int(std::count_if(m_problems.cbegin(), m_problems.cend(), [](const GpsProblems::Problem &problem)
    {
        return problem.fixable();
    }));
    m_summary->setText(m_problems.isEmpty() ? tr("The positions look right: no gaps, jumps or frozen stretches.")
                       : tr("Found: %1, to fix: %2. Select one to go there.").arg(m_problems.size()).arg(fixable));
    updateButtons();
    QDialog::show();
    raise();
    activateWindow();
}

void GpsProblemsDialog::fix(bool all)
{
    QVector<DataEdit> edits = m_edits;
    for (int i = 0; i < m_problems.size(); ++i)
        {
            if (m_problems.at(i).fixable() && (all || m_list->topLevelItem(i)->isSelected()))
                {
                    edits.append(m_problems.at(i).fix());
                }
        }
    if (edits.size() > m_edits.size())
        {
            emit fixed(edits, all ? tr("Fix all GPS problems") : tr("Fix GPS problems"));
        }
}

void GpsProblemsDialog::updateButtons()
{
    bool selected = false;
    bool any = false;
    for (int i = 0; i < m_problems.size(); ++i)
        {
            any = any || m_problems.at(i).fixable();
            selected = selected || (m_problems.at(i).fixable() && m_list->topLevelItem(i)->isSelected());
        }
    m_fixButton->setEnabled(selected);
    m_fixAllButton->setEnabled(any);
}
