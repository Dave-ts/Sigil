/************************************************************************
**
**  Copyright (C) 2012 John Schember <john@nachtimwald.com>
**  Copyright (C) 2012 Dave Heiland
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include <QtCore/QCoreApplication>
#include <QByteArray>
#include <QDataStream>

#include "MiscEditors/ClipEditorModel.h"

static const QString SETTINGS_GROUP         = "clip_entries";
static const QString ENTRY_NAME             = "Name";
static const QString ENTRY_TEXT             = "Text";

const int COLUMNS = 2;

static const int IS_GROUP_ROLE = Qt::UserRole + 1;
static const int FULLNAME_ROLE = Qt::UserRole + 2;

static const QString CLIP_EXAMPLE_FILE = "clip_examples.ini";

ClipEditorModel *ClipEditorModel::m_instance = 0;

ClipEditorModel *ClipEditorModel::instance()
{
    if (m_instance == 0) {
        m_instance = new ClipEditorModel();
    }

    return m_instance;
}

ClipEditorModel::ClipEditorModel(QObject *parent)
 : QStandardItemModel(parent)
{
    LoadInitialData();

    connect(this, SIGNAL(itemChanged(QStandardItem*)),
            this, SLOT(ItemChangedHandler(QStandardItem*)));
}

ClipEditorModel::~ClipEditorModel()
{
    if (m_instance) {
        delete m_instance;
        m_instance = 0;
    }
}
 
Qt::DropActions ClipEditorModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QMimeData* ClipEditorModel::mimeData(const QModelIndexList &indexes) const 
{
    QMimeData *mimeData = new QMimeData();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    // mimeData index is not the index of the actual item
    // index is the index of the first entry in the group
    // index.row() is the row of our actual index in the group
    // So you need index.parent()->child(index.row(),0) to get the actual index!
    foreach (QModelIndex index, indexes) {
        if (index.isValid() && index.column() == 0) {
            stream << index.internalId() << index.row();
        }
    }

    mimeData->setData("x-index", encodedData);
    return mimeData;
}

bool ClipEditorModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex & parent)
{
    if (action == Qt::IgnoreAction) {
        return true;
    }

    if (!data->hasFormat("x-index")) {
        return false;
    }

    // If dropped on an non-group entry convert to parent item group/row
    QModelIndex new_parent = parent;
    if (parent.isValid() && !itemFromIndex(parent)->data(IS_GROUP_ROLE).toBool()) {
        row = parent.row() + 1;
        new_parent = parent.parent();
    }

    QByteArray encodedData = data->data("x-index");
    QDataStream stream(&encodedData, QIODevice::ReadOnly);

    QList<QStandardItem *> items;

    while (!stream.atEnd()) {
        qint64 id;
        int row;

        stream >> id >> row;
        QStandardItem* item = GetItemFromId(id, row);
        items.append(item);
    }

    // Abort if any items appear more than once in the selection
    QList<QStandardItem*> all_items;
    foreach (QStandardItem* item, items) {
        all_items.append(GetNonParentItems(item));
    }
    if (all_items.count() != all_items.toSet().count()) {
        return false;
    }

    foreach (QStandardItem* item, items) {
        QStandardItem *parent_item = NULL;
        if (new_parent.isValid()) {
            parent_item = itemFromIndex(new_parent);
        }

        // Get the parent path of the item so it can be moved
        QString parent_path = "";
        if (item->parent()) {
            parent_path = item->parent()->data(FULLNAME_ROLE).toString();
        }

        // Move all subitems of an item not just the item itself
        QList<QStandardItem*> sub_items = GetNonParentItems(item);

        if (item->data(IS_GROUP_ROLE).toBool()) {
            ClipEditorModel::clipEntry *top_group_entry = GetEntry(item);
            parent_item = AddEntryToModel(top_group_entry, top_group_entry->is_group, parent_item, row);
            parent_path = item->data(FULLNAME_ROLE).toString();
        }

        foreach (QStandardItem* item, sub_items) {
            ClipEditorModel::clipEntry *entry = GetEntry(item);

            // Remove the top level paths
            entry->fullname.replace(QRegExp(parent_path), "");
            entry->name = entry->fullname;

            AddFullNameEntry(entry, parent_item, row);
            if (row >= 0) {
                row++;
            }
        }
    }

    return true;
}

void ClipEditorModel::ItemChangedHandler(QStandardItem *item)
{
    Q_ASSERT(item);

    if (item->column() != 0) {
        return;
    }

    // Restore name if nothing entered or contains a group indicator
    if (item->text().isEmpty() || item->text().contains("/")) {
        QString name = item->text();
        name.replace(QRegExp("/$"), "");
        if (name.contains("/")) {
            name = name.split("/").last();
        }
        item->setText(name);
        return;
    }

    Rename(item);
}

void ClipEditorModel::Rename(QStandardItem *item, QString name)
{
    // Update the name and all its children
    // Disconnect change signal while changing the items
    disconnect(this, SIGNAL(itemChanged(       QStandardItem*)),
               this, SLOT(  ItemChangedHandler(QStandardItem*)));

    // If item name not already changed, set it
    if (name != "") {
        item->setText(name);
    }

    UpdateFullName(item);

    connect(this, SIGNAL(itemChanged(       QStandardItem*)),
            this, SLOT(  ItemChangedHandler(QStandardItem*)));
}

void ClipEditorModel::UpdateFullName(QStandardItem *item)
{
    QStandardItem *parent_item = item->parent();
    if (!parent_item) {
        parent_item = invisibleRootItem();
    }

    QString fullname = parent_item->data(FULLNAME_ROLE).toString() + item->text();
    if (item->data(IS_GROUP_ROLE).toBool()) {
        fullname.append("/");
    }

    item->setToolTip(fullname);
    item->setData(fullname, FULLNAME_ROLE);

    for (int row = 0; row < item->rowCount(); row++) {
        UpdateFullName(item->child(row,0));
    }
}

bool ClipEditorModel::ItemIsGroup(QStandardItem* item)
{
    return item->data(IS_GROUP_ROLE).toBool();
}

QString ClipEditorModel::GetFullName(QStandardItem* item)
{
    return item->data(FULLNAME_ROLE).toString();
}

ClipEditorModel::clipEntry* ClipEditorModel::GetEntryFromName(QString name, QStandardItem *item)
{
    return GetEntry(GetItemFromName(name, item));
}

QStandardItem* ClipEditorModel::GetItemFromName(QString name, QStandardItem *item)
{
    QStandardItem* found_item = NULL;

    if (!item) {
        item = invisibleRootItem();
    }

    if (item != invisibleRootItem() && item->data(FULLNAME_ROLE).toString() == name) {
        return item;
    }

    for (int row = 0; row < item->rowCount(); row++) {
        found_item = GetItemFromName(name, item->child(row,0));

        // Return with first found entry
        if (found_item) {
            return found_item;
        }
    }

    return found_item;
}

void ClipEditorModel::LoadInitialData()
{
    clear();

    QStringList header;
    header.append(tr("Name"));
    header.append(tr("Text"));

    setHorizontalHeaderLabels(header);

    LoadData();

    if (invisibleRootItem()->rowCount() == 0) {
        AddExampleEntries();
    }
}

void ClipEditorModel::LoadData(QString filename, QStandardItem *item)
{
    SettingsStore *settings;
    if (filename.isEmpty()) {
        settings = new SettingsStore();
    }
    else {
        settings = new SettingsStore(filename);
    }

    int size = settings->beginReadArray(SETTINGS_GROUP);

    // Add one entry at a time to the list
    for (int i = 0; i < size; ++i) {
        settings->setArrayIndex(i);

        ClipEditorModel::clipEntry *entry = new ClipEditorModel::clipEntry();

        QString fullname = settings->value(ENTRY_NAME).toString();
        fullname.replace(QRegExp("\\s*/+\\s*"), "/");
        fullname.replace(QRegExp("^/"), "");

        entry->is_group = fullname.endsWith("/");

        // Name is set to fullname only while looping through parent groups when adding
        entry->name = fullname;
        entry->fullname = fullname;
        entry->text = settings->value(ENTRY_TEXT).toString();

        AddFullNameEntry(entry, item);
    }

    settings->endArray();
}

void ClipEditorModel::AddFullNameEntry(ClipEditorModel::clipEntry *entry, QStandardItem *parent_item, int row)
{
    if (!parent_item) {
        parent_item = invisibleRootItem();
    }

    if (parent_item != invisibleRootItem() && !parent_item->data(IS_GROUP_ROLE).toBool()) {
        row = parent_item->row() + 1;
        parent_item = parent_item->parent();
    }

    if (row < 0) {
        row = parent_item->rowCount();
    }

    QString entry_name = entry->name;

    if (entry->name.contains("/")) {
        QStringList group_names = entry->name.split("/", QString::SkipEmptyParts);
        entry_name = group_names.last();
        if (!entry->is_group) {
            group_names.removeLast();
        }

        foreach (QString group_name, group_names) {
            bool found = false;
            for (int r = 0; r < parent_item->rowCount(); r++) {
                if (parent_item->child(r, 0)->data(IS_GROUP_ROLE).toBool() && parent_item->child(r, 0)->text() == group_name) {
                    parent_item = parent_item->child(r, 0);
                    found = true;
                    break;
                }
            }
            if (!found) {
                ClipEditorModel::clipEntry *new_entry = new ClipEditorModel::clipEntry();
                new_entry->is_group = true;
                new_entry->name = group_name;
                parent_item = AddEntryToModel(new_entry, new_entry->is_group, parent_item, parent_item->rowCount());
            }
        }
        row = parent_item->rowCount();
    }

    if (!entry->is_group) {
        entry->name = entry_name;
        AddEntryToModel(entry, entry->is_group, parent_item, row);
    }
}

QStandardItem *ClipEditorModel::AddEntryToModel(ClipEditorModel::clipEntry *entry, bool is_group, QStandardItem *parent_item, int row)
{
    // parent_item must be a group item

    if (!parent_item) {
        parent_item = invisibleRootItem();
    }

    // Create an empty entry if none supplied
    if (!entry) {
        entry = new ClipEditorModel::clipEntry();
        entry->is_group = is_group;
        if (!is_group) {
            entry->name = "Text";
            entry->text = "";
        }
        else {
            entry->name = "Group";
        }
    }

    entry->fullname = entry->name;
    if (parent_item != invisibleRootItem()) {
        // Set the fullname based on the parent entry
        entry->fullname = parent_item->data(FULLNAME_ROLE).toString() + entry->name;
    }

    if (entry->is_group) {
        entry->fullname += "/";
    }

    QList<QStandardItem *> rowItems;
    QStandardItem *group_item = parent_item;

    if (entry->is_group) {
        group_item = new QStandardItem(entry->name);
        QFont font = *new QFont();
        font.setWeight(QFont::Bold);
        group_item->setFont(font);

        rowItems << group_item;
        for (int col = 1; col < COLUMNS ; col++) {
            QStandardItem *item = new QStandardItem("");
            item->setEditable(false);
            rowItems << item;
        }
    }
    else {
        rowItems << new QStandardItem(entry->name);
        rowItems << new QStandardItem(entry->text);
    }

    rowItems[0]->setData(entry->is_group, IS_GROUP_ROLE);
    rowItems[0]->setData(entry->fullname, FULLNAME_ROLE);
    rowItems[0]->setToolTip(entry->fullname);

    // Add the new item to the model at the specified row
    QStandardItem *new_item;
    if (row < 0 || row >= parent_item->rowCount()) {
        parent_item->appendRow(rowItems);
        new_item = parent_item->child(parent_item->rowCount() - 1, 0);
    }
    else {
        parent_item->insertRow(row, rowItems);
        new_item = parent_item->child(row, 0);
    }

    return new_item;
}

void ClipEditorModel::AddExampleEntries()
{
    QString example_file = QCoreApplication::applicationDirPath() + "/../share/" + QCoreApplication::applicationName().toLower() + "/examples/" + CLIP_EXAMPLE_FILE;

    LoadData(example_file);
}

QList<QStandardItem*> ClipEditorModel::GetItemsForIndexes(QModelIndexList indexes)
{
    QList<QStandardItem*> items;

    foreach (QModelIndex index, indexes) {
        if (index.column() == 0) {
            items.append(itemFromIndex(index));
        }
    }

    return items;
}

QList<QStandardItem *> ClipEditorModel::GetNonGroupItems(QList<QStandardItem *> items)
{
    QList<QStandardItem *> all_items;

    foreach (QStandardItem *item, items) {
        all_items.append(GetNonGroupItems(item));
    }

    return all_items;
}

QList<QStandardItem *> ClipEditorModel::GetNonGroupItems(QStandardItem* item)
{
    QList<QStandardItem *> items;

    if (!item->data(IS_GROUP_ROLE).toBool()) {
        items.append(item);
    }
    for (int row = 0; row < item->rowCount(); row++) {
        items.append(GetNonGroupItems(item->child(row, 0)));
    }

    return items;
}

QList<QStandardItem *> ClipEditorModel::GetNonParentItems(QList<QStandardItem *> items)
{
    QList<QStandardItem *> all_items;

    foreach (QStandardItem *item, items) {
        all_items.append(GetNonParentItems(item));
    }

    return all_items;
}

QList<QStandardItem *> ClipEditorModel::GetNonParentItems(QStandardItem* item)
{
    QList<QStandardItem *> items;

    if (item->rowCount() == 0) {
        if (item != invisibleRootItem()) {
            items.append(item);
        }
    }

    for (int row = 0; row < item->rowCount(); row++) {
        items.append(GetNonParentItems(item->child(row, 0)));
    }

    return items;
}

QList<ClipEditorModel::clipEntry *> ClipEditorModel::GetEntries(QList<QStandardItem*> items)
{
    QList<ClipEditorModel::clipEntry *> entries;

    foreach (QStandardItem *item, items) {
        entries.append(GetEntry(item));
    }
    return entries;
}

ClipEditorModel::clipEntry* ClipEditorModel::GetEntry(QStandardItem *item)
{
    QStandardItem *parent_item;

    if (item->parent()) {
        parent_item = item->parent();
    }
    else {
        parent_item = invisibleRootItem();
    }

    ClipEditorModel::clipEntry *entry = new ClipEditorModel::clipEntry();

    entry->is_group =    parent_item->child(item->row(), 0)->data(IS_GROUP_ROLE).toBool();
    entry->fullname =    parent_item->child(item->row(), 0)->data(FULLNAME_ROLE).toString();
    entry->name =        parent_item->child(item->row(), 0)->text();
    entry->text =        parent_item->child(item->row(), 1)->text();

    // Convert the mode values from translated text into enumerated modes

    return entry;
}

QStandardItem* ClipEditorModel::GetItemFromId(qint64 id, int row, QStandardItem *item) const
{
    QStandardItem* found_item = NULL;

    if (!item) {
        item = invisibleRootItem();
    }

    if (item->index().internalId() == id) {
        if (item->parent()) {
            item = item->parent();
        }
        else {
            item = invisibleRootItem();
        }
        return item->child(row, 0);
    }

    for (int r = 0; r < item->rowCount(); r++) {
        found_item = GetItemFromId(id, row, item->child(r,0));

        // Return with first found entry
        if (found_item) {
            return found_item;
        }
    }

    return found_item;
}

QString ClipEditorModel::SaveData(QList<ClipEditorModel::clipEntry*> entries, QString filename)
{
    QString message = "";

    // Save everything if no entries selected
    if (entries.isEmpty()) {
        QList<QStandardItem *> items = GetNonParentItems(invisibleRootItem());
        if (!items.isEmpty()) {
            entries = GetEntries(items);
        }
    }

    // Open the default file for save, or specific file for export
    SettingsStore *settings;
    if (filename.isEmpty()) {
        settings = new SettingsStore();
    }
    else {
        settings = new SettingsStore(filename);
    }

    settings->sync();
    if (!settings->isWritable()) {
        message = tr("Unable to create file %1").arg(filename);
        return message;
    }

    // Remove the old values to account for deletions
    settings->remove(SETTINGS_GROUP);

    settings->beginWriteArray(SETTINGS_GROUP);

    int i = 0;
    foreach (ClipEditorModel::clipEntry *entry, entries) {
        settings->setArrayIndex(i++);
        settings->setValue(ENTRY_NAME, entry->fullname);

        if (!entry->is_group) {
            settings->setValue(ENTRY_TEXT, entry->text);
        }
    }

    settings->endArray();

    // Make sure file is created/updated so it can be checked
    settings->sync();

    return message;
}
