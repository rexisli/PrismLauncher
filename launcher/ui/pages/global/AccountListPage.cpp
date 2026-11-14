// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "AccountListPage.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui_AccountListPage.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMenu>
#include <QPushButton>

#include <QDebug>

#include "ui/dialogs/ChooseOfflineNameDialog.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/MSALoginDialog.h"

#include "Application.h"

AccountListPage::AccountListPage(QWidget* parent) : QMainWindow(parent), ui(new Ui::AccountListPage)
{
    ui->setupUi(this);
    ui->listView->setEmptyString(
        tr("Welcome!\n"
           "If you're new here, you can select \"Add Microsoft\" to link your Microsoft account, "
           "or \"Add Offline\" to play without one."));
    ui->listView->setEmptyMode(VersionListView::String);
    ui->listView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_accounts = APPLICATION->accounts();

    ui->listView->setModel(m_accounts);
    ui->listView->header()->setSectionResizeMode(AccountList::VListColumns::ProfileNameColumn, QHeaderView::Stretch);
    ui->listView->header()->setSectionResizeMode(AccountList::VListColumns::TypeColumn, QHeaderView::ResizeToContents);
    ui->listView->header()->setSectionResizeMode(AccountList::VListColumns::StatusColumn, QHeaderView::ResizeToContents);
    ui->listView->setSelectionMode(QAbstractItemView::SingleSelection);

    // Expand the account column

    QItemSelectionModel* selectionModel = ui->listView->selectionModel();

    connect(selectionModel, &QItemSelectionModel::selectionChanged, this,
            [this]([[maybe_unused]] const QItemSelection& sel, [[maybe_unused]] const QItemSelection& dsel) { updateButtonStates(); });
    connect(ui->listView, &VersionListView::customContextMenuRequested, this, &AccountListPage::ShowContextMenu);
    connect(ui->listView, &VersionListView::activated, this,
            [this](const QModelIndex& index) { m_accounts->setDefaultAccount(m_accounts->at(index.row())); });

    connect(m_accounts, &AccountList::listChanged, this, &AccountListPage::listChanged);
    connect(m_accounts, &AccountList::listActivityChanged, this, &AccountListPage::listChanged);
    connect(m_accounts, &AccountList::defaultAccountChanged, this, &AccountListPage::listChanged);

    updateButtonStates();

    // Xbox authentication won't work without a client identifier, so disable the button if it is missing
    if (~APPLICATION->capabilities() & Application::SupportsMSA) {
        ui->actionAddMicrosoft->setVisible(false);
        ui->actionAddMicrosoft->setToolTip(tr("No Microsoft Authentication client ID was set."));
    }
}

AccountListPage::~AccountListPage()
{
    delete ui;
}

void AccountListPage::retranslate()
{
    ui->retranslateUi(this);
}

void AccountListPage::ShowContextMenu(const QPoint& pos)
{
    auto menu = ui->toolBar->createContextMenu(this, tr("Context menu"));
    menu->exec(ui->listView->mapToGlobal(pos));
    delete menu;
}

void AccountListPage::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    QMainWindow::changeEvent(event);
}

QMenu* AccountListPage::createPopupMenu()
{
    QMenu* filteredMenu = QMainWindow::createPopupMenu();
    filteredMenu->removeAction(ui->toolBar->toggleViewAction());
    return filteredMenu;
}

void AccountListPage::listChanged()
{
    updateButtonStates();
}

void AccountListPage::on_actionAddMicrosoft_triggered()
{
    auto account = MSALoginDialog::newAccount(this);
    if (account) {
        m_accounts->addAccount(account);
        if (m_accounts->count() == 1) {
            m_accounts->setDefaultAccount(account);
        }
    }
}

void AccountListPage::on_actionAddOffline_triggered()
{
    ChooseOfflineNameDialog dialog(tr("Please enter your desired username to add your offline account."), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (const MinecraftAccountPtr account = MinecraftAccount::createOffline(dialog.getUsername(), dialog.getUuid())) {
        account->login()->start();  // The task will complete here.
        m_accounts->addAccount(account);
        if (m_accounts->count() == 1) {
            m_accounts->setDefaultAccount(account);
        }
    }
}

void AccountListPage::on_actionEditOffline_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.empty()) {
        return;
    }

    MinecraftAccountPtr account = selection.first().data(AccountList::PointerRole).value<MinecraftAccountPtr>();
    if (!account || account->accountType() != AccountType::Offline) {
        return;
    }

    const QString oldUuid = account->profileId();

    ChooseOfflineNameDialog dialog(tr("Edit the username and UUID of this offline account."), this);
    dialog.setWindowTitle(tr("Edit Offline Account"));
    dialog.setUsername(account->profileName());
    dialog.setUuid(oldUuid);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newUuid = MinecraftAccount::normalizeUuid(dialog.getUuid());
    if (newUuid != oldUuid) {
        auto response = CustomMessageBox::selectable(this, tr("Change the UUID?"),
                                                    tr("Singleplayer worlds, servers and statistics identify a player by its UUID. "
                                                       "Changing it makes existing worlds treat this account as a different player."),
                                                    QMessageBox::Question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                            ->exec();
        if (response != QMessageBox::Yes) {
            return;
        }
    }

    account->setOfflineProfile(dialog.getUsername(), dialog.getUuid());
}

void AccountListPage::on_actionImportHmcl_triggered()
{
    QString baseDir = qEnvironmentVariable("APPDATA");
    if (baseDir.isEmpty()) {
        baseDir = QDir::homePath();
    }

    const QString path =
        QFileDialog::getOpenFileName(this, tr("Select an HMCL accounts.json"), QDir(baseDir).filePath(".hmcl/accounts.json"),
                                     tr("HMCL accounts file (accounts.json);;JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import failed"), tr("Could not open %1.").arg(path));
        return;
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("Import failed"), tr("Could not parse %1: %2").arg(path, parseError.errorString()));
        return;
    }

    QJsonArray entries;
    if (document.isArray()) {
        entries = document.array();
    } else if (document.isObject()) {
        const auto object = document.object();
        if (object.value("accounts").isArray()) {
            entries = object.value("accounts").toArray();
        } else if (object.value("username").isString() || object.value("profileName").isString()) {
            entries.append(object);
        }
    }
    if (entries.isEmpty()) {
        QMessageBox::warning(this, tr("Import failed"), tr("No accounts were found in %1.").arg(path));
        return;
    }

    struct HmclAccount {
        QString name;
        QString uuid;
    };

    QList<HmclAccount> candidates;
    for (const auto& value : entries) {
        if (!value.isObject()) {
            continue;
        }
        const auto entry = value.toObject();
        if (entry.value("type").toString() == "msa") {
            continue;
        }
        QString name = entry.value("displayName").toString();
        if (name.isEmpty()) {
            name = entry.value("profileName").toString();
        }
        if (name.isEmpty()) {
            name = entry.value("username").toString();
        }
        if (name.isEmpty()) {
            continue;
        }
        QString uuid = entry.value("uuid").toString();
        if (uuid.isEmpty()) {
            uuid = entry.value("profileID").toString();
        }
        uuid = MinecraftAccount::normalizeUuid(uuid);

        bool duplicate = false;
        for (const auto& candidate : candidates) {
            if (uuid.isEmpty() ? candidate.uuid.isEmpty() && candidate.name == name : candidate.uuid == uuid) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            candidates.append({ name, uuid });
        }
    }

    int imported = 0;
    int updated = 0;
    int skipped = 0;
    for (const auto& candidate : candidates) {
        if (!candidate.uuid.isEmpty() && m_accounts->findAccountByProfileId(candidate.uuid) != -1) {
            skipped++;
            continue;
        }

        MinecraftAccountPtr sameNameAccount;
        for (int i = 0; i < m_accounts->count(); i++) {
            const auto account = m_accounts->at(i);
            if (account->accountType() == AccountType::Offline && account->profileName() == candidate.name) {
                sameNameAccount = account;
                break;
            }
        }
        if (sameNameAccount) {
            sameNameAccount->setOfflineProfile(candidate.name, candidate.uuid);
            updated++;
            continue;
        }

        const auto account = MinecraftAccount::createOffline(candidate.name, candidate.uuid);
        account->login()->start();  // The task will complete here.
        m_accounts->addAccount(account);
        if (m_accounts->count() == 1) {
            m_accounts->setDefaultAccount(account);
        }
        imported++;
    }

    QMessageBox::information(this, tr("Import finished"),
                             tr("Imported %1 account(s), updated %2 account(s), skipped %3 account(s).")
                                 .arg(imported)
                                 .arg(updated)
                                 .arg(skipped));
}

void AccountListPage::on_actionRemove_triggered()
{
    auto response = CustomMessageBox::selectable(this, tr("Remove account?"), tr("Do you really want to delete this account?"),
                                                 QMessageBox::Question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();
    if (response != QMessageBox::Yes) {
        return;
    }
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        m_accounts->removeAccount(selected);
    }
}

void AccountListPage::on_actionRefresh_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        m_accounts->requestRefresh(account->internalId());
    }
}

void AccountListPage::on_actionSetDefault_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        m_accounts->setDefaultAccount(account);
    }
}

void AccountListPage::on_actionNoDefault_triggered()
{
    m_accounts->setDefaultAccount(nullptr);
}

void AccountListPage::updateButtonStates()
{
    // If there is no selection, disable buttons that require something selected.
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    bool hasSelection = !selection.empty();
    bool accountIsReady = false;
    bool accountIsOnline = false;
    bool accountCanMoveUp = false;
    bool accountCanMoveDown = false;
    if (hasSelection) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        accountIsReady = !account->isActive();
        accountIsOnline = account->accountType() != AccountType::Offline;

        accountCanMoveUp = selected.row() > 0;
        int indexOfLast = m_accounts->count() - 1;
        accountCanMoveDown = selected.row() < indexOfLast;
    }
    ui->actionRemove->setEnabled(accountIsReady);
    ui->actionSetDefault->setEnabled(accountIsReady);
    ui->actionManageSkins->setEnabled(accountIsReady && accountIsOnline);
    ui->actionRefresh->setEnabled(accountIsReady && accountIsOnline);
    ui->actionEditOffline->setEnabled(accountIsReady && !accountIsOnline);

    if (m_accounts->defaultAccount().get() == nullptr) {
        ui->actionNoDefault->setEnabled(false);
        ui->actionNoDefault->setChecked(true);
    } else {
        ui->actionNoDefault->setEnabled(true);
        ui->actionNoDefault->setChecked(false);
    }
    ui->actionMoveUp->setEnabled(accountCanMoveUp);
    ui->actionMoveDown->setEnabled(accountCanMoveDown);
    ui->listView->resizeColumnToContents(3);
}

void AccountListPage::on_actionManageSkins_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        SkinManageDialog dialog(this, account);
        dialog.exec();
    }
}

void AccountListPage::on_actionMoveUp_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        m_accounts->moveAccount(selected, -1);
    }
}

void AccountListPage::on_actionMoveDown_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        m_accounts->moveAccount(selected, 1);
    }
}
