/*
 * tilestampmodel.cpp
 * Copyright 2015, Thorbjørn Lindeijer <bjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tilestampmodel.h"

#include "thumbnailrenderer.h"
#include "tilestamp.h"

namespace Tiled {
namespace Internal {

TileStampModel::TileStampModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex TileStampModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    if (!parent.isValid())
        return createIndex(row, column);
    else if (isStamp(parent))
        return createIndex(row, column, parent.row() + 1);

    return QModelIndex();
}

QModelIndex TileStampModel::parent(const QModelIndex &index) const
{
    if (quintptr id = index.internalId())
        return createIndex(id - 1, 0);

    return QModelIndex();
}

int TileStampModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return mStamps.size();
    } else if (isStamp(parent)) {
        const TileStamp &stamp = mStamps.at(parent.row());
        const int count = stamp.variations().size();
        // it does not make much sense to expand single variations
        return count == 1 ? 0 : count;
    }

    return 0;
}

int TileStampModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 2; // stamp | probability
}

QVariant TileStampModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case 0: return tr("Stamp");
        case 1: return tr("Probability");
        }
    }
    return QVariant();
}

bool TileStampModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (isStamp(index)) {
        TileStamp &stamp = mStamps[index.row()];
        if (index.column() == 0) {
            switch (role) {
            case Qt::EditRole:
                stamp.setName(value.toString());
                return true;
                break;
            default:
                break;
            }
        }
    } else {
        QModelIndex parent = index.parent();
        if (isStamp(parent)) {
            TileStamp &stamp = mStamps[parent.row()];
            stamp.setProbability(index.row(), value.toReal());
            return true;
        }
    }

    return false;
}

static QPixmap renderThumbnail(const ThumbnailRenderer &renderer)
{
    return QPixmap::fromImage(renderer.render(QSize(64, 64))
                              .scaled(32, 32,
                                      Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation));
}

QVariant TileStampModel::data(const QModelIndex &index, int role) const
{
    if (isStamp(index)) {
        const TileStamp &stamp = mStamps.at(index.row());
        if (index.column() == 0) {
            switch (role) {
            case Qt::DisplayRole:
            case Qt::EditRole:
                return stamp.name();
            case Qt::DecorationRole: {
                Map *map = stamp.variations().first().map;
                QPixmap thumbnail = mThumbnailCache.value(map);
                if (thumbnail.isNull()) {
                    ThumbnailRenderer renderer(map);
                    thumbnail = renderThumbnail(renderer);
                    mThumbnailCache.insert(map, thumbnail);
                }
                return thumbnail;
                break;
            }
            }
        }
    } else if (const TileStampVariation *variation = variationAt(index)) {
        if (index.column() == 0) {
            switch (role) {
            case Qt::DecorationRole: {
                Map *map = variation->map;
                QPixmap thumbnail = mThumbnailCache.value(map);
                if (thumbnail.isNull()) {
                    ThumbnailRenderer renderer(map);
                    thumbnail = renderThumbnail(renderer);
                    mThumbnailCache.insert(map, thumbnail);
                }
                return thumbnail;
                break;
            }
            }
        } else if (index.column() == 1) {
            switch (role) {
            case Qt::DisplayRole:
            case Qt::EditRole:
                return variation->probability;
            }
        }
    }

    return QVariant();
}

Qt::ItemFlags TileStampModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags rc = QAbstractItemModel::flags(index);
    const bool validParent = index.parent().isValid();
    if ((!validParent && index.column() == 0) ||   // can edit stamp names
            (validParent && index.column() == 1))  // and variation probability
        rc |= Qt::ItemIsEditable;
    return rc;
}

bool TileStampModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid()) {
        // removing variations
        TileStamp &stamp = mStamps[parent.row()];

        // if only one stamp is left, we make all variation rows disappear
        if (stamp.variations().size() - count == 1)
            beginRemoveRows(parent, 0, count);
        else
            beginRemoveRows(parent, row, row + count - 1);

        for (; count > 0; --count) {
            mThumbnailCache.remove(stamp.variations().at(row).map);
            stamp.deleteVariation(row);
        }
        endRemoveRows();

        if (stamp.variations().isEmpty()) {
            // remove stamp since all its variations were removed
            beginRemoveRows(QModelIndex(), parent.row(), parent.row());
            mStamps.removeAt(parent.row());
            endRemoveRows();
        } else if (row == 0) {
            // preview on stamp row needs update
            const QModelIndex modelIndex = index(parent.row(), 0);
            emit dataChanged(modelIndex, modelIndex);
        }
    } else {
        // removing stamps
        beginRemoveRows(parent, row, row + count - 1);
        for (; count > 0; --count) {
            for (const TileStampVariation &variation : mStamps.at(row).variations())
                mThumbnailCache.remove(variation.map);
            mStamps.removeAt(row);
        }
        endRemoveRows();
    }

    return true;
}

const TileStamp &TileStampModel::stampAt(const QModelIndex &index) const
{
    Q_ASSERT(index.isValid());
    Q_ASSERT(!index.parent().isValid()); // stamps don't have parents

    return mStamps.at(index.row());
}

bool TileStampModel::isStamp(const QModelIndex &index) const
{
    return index.isValid()
            && !index.parent().isValid()
            && index.row() < mStamps.size();
}

const TileStampVariation *TileStampModel::variationAt(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;

    QModelIndex parent = index.parent();
    if (isStamp(parent)) {
        const TileStamp &stamp = mStamps.at(parent.row());
        return &stamp.variations().at(index.row());
    }

    return 0;
}

void TileStampModel::addStamp(const TileStamp &stamp)
{
    if (mStamps.contains(stamp))
        return;

    beginInsertRows(QModelIndex(), mStamps.size(), mStamps.size());
    mStamps.append(stamp);
    endInsertRows();
}

void TileStampModel::removeStamp(const TileStamp &stamp)
{
    int index = mStamps.indexOf(stamp);
    if (index == -1)
        return;

    beginRemoveRows(QModelIndex(), index, index);
    mStamps.removeAt(index);
    endRemoveRows();

    for (const TileStampVariation &variation : stamp.variations())
        mThumbnailCache.remove(variation.map);
}

void TileStampModel::addVariation(const TileStamp &stamp,
                                  const TileStampVariation &variation)
{
    int index = mStamps.indexOf(stamp);
    if (index == -1)
        return;

    const int variationCount = stamp.variations().size();

    if (variationCount == 1)
        beginInsertRows(TileStampModel::index(index, 0), 0, 1);
    else
        beginInsertRows(TileStampModel::index(index, 0),
                        variationCount, variationCount);

    mStamps[index].addVariation(variation);
    endInsertRows();
}

} // namespace Internal
} // namespace Tiled
