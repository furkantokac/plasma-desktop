/***************************************************************************
 *   Copyright (C) 2014-2015 by Eike Hein <hein@kde.org>                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "recentappsmodel.h"
#include "actionlist.h"
#include "appsmodel.h"
#include "appentry.h"

#include <config-X11.h>

#include <QIcon>
#if HAVE_X11
#include <QX11Info>
#endif

#include <KActivities/ResourceInstance>
#include <KLocalizedString>
#include <KRun>
#include <KService>
#include <KStartupInfo>

#include <KActivitiesExperimentalStats/Cleaning>
#include <KActivitiesExperimentalStats/ResultModel>
#include <KActivitiesExperimentalStats/Terms>

namespace KAStats = KActivities::Experimental::Stats;

using namespace KAStats;
using namespace KAStats::Terms;

RecentAppsModel::RecentAppsModel(QObject *parent) : ForwardingModel(parent)
{
    refresh();
}

RecentAppsModel::~RecentAppsModel()
{
}

QString RecentAppsModel::description() const
{
    return i18n("Applications");
}

QVariant RecentAppsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    const QString storageId = sourceModel()->data(index, ResultModel::ResourceRole).toString().section(':', 1);
    KService::Ptr service = KService::serviceByStorageId(storageId);

    if (!service || !service->isApplication()) {
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
            return AppEntry::nameFromService(service,
                (AppEntry::NameFormat)qobject_cast<AppsModel *>(QObject::parent())->appNameFormat());
    } else if (role == Qt::DecorationRole) {
        return QIcon::fromTheme(service->icon(), QIcon::fromTheme("unknown"));
    } else if (role == Kicker::FavoriteIdRole) {
        return service->storageId();
    } else if (role == Kicker::HasActionListRole) {
        return true;
    } else if (role == Kicker::ActionListRole) {
        QVariantList actionList;

        actionList << Kicker::recentDocumentActions(service);

        if (actionList.count()) {
            actionList << Kicker::createSeparatorActionItem();
        }

        const QVariantMap &forgetAction = Kicker::createActionItem(i18n("Forget Application"), "forget");
        actionList << forgetAction;

        const QVariantMap &forgetAllAction = Kicker::createActionItem(i18n("Forget All Applications"), "forgetAll");
        actionList << forgetAllAction;

        return actionList;
    }

    return QVariant();
}

bool RecentAppsModel::trigger(int row, const QString &actionId, const QVariant &argument)
{
    Q_UNUSED(argument)

    bool withinBounds = row >= 0 && row < rowCount();

    if (actionId.isEmpty() && withinBounds) {
        const QString storageId = sourceModel()->data(sourceModel()->index(row, 0),
            ResultModel::ResourceRole).toString().section(':', 1);
        KService::Ptr service = KService::serviceByStorageId(storageId);

        if (!service) {
            return false;
        }

        quint32 timeStamp = 0;

#if HAVE_X11
        if (QX11Info::isPlatformX11()) {
            timeStamp = QX11Info::appUserTime();
        }
#endif

        new KRun(QUrl::fromLocalFile(service->entryPath()), 0, true,
            KStartupInfo::createNewStartupIdForTimestamp(timeStamp));

        KActivities::ResourceInstance::notifyAccessed(QUrl("applications:" + storageId),
            "org.kde.plasma.kicker");

        return true;
    } else if (actionId == "forget" && withinBounds) {
        if (sourceModel()) {
            ResultModel *resultModel = static_cast<ResultModel *>(sourceModel());
            resultModel->forgetResource(row);
        }

        return false;
    } else if (actionId == "forgetAll") {
        if (sourceModel()) {
            ResultModel *resultModel = static_cast<ResultModel *>(sourceModel());
            resultModel->forgetAllResources();
        }

        return false;
    } else if (withinBounds) {
        const QString storageId = sourceModel()->data(sourceModel()->index(row, 0),
            ResultModel::ResourceRole).toString().section(':', 1);
        KService::Ptr service = KService::serviceByStorageId(storageId);

        if (service) {
            return Kicker::handleRecentDocumentAction(service, actionId, argument);
        }
    }

    return false;
}

bool RecentAppsModel::hasActions() const
{
    return rowCount();
}

QVariantList RecentAppsModel::actions() const
{
    QVariantList actionList;

    if (rowCount()) {
        actionList << Kicker::createActionItem(i18n("Forget All Applications"), "forgetAll");
    }

    return actionList;
}

void RecentAppsModel::refresh()
{
    QObject *oldModel = sourceModel();

    auto query = UsedResources
                    | RecentlyUsedFirst
                    | Agent::any()
                    | Type::any()
                    | Activity::current()
                    | Url::startsWith("applications:")
                    | Limit(15);

    ResultModel *model = new ResultModel(query);

    QModelIndex index;

    if (model->canFetchMore(index)) {
        model->fetchMore(index);
    }

    setSourceModel(model);

    delete oldModel;
}