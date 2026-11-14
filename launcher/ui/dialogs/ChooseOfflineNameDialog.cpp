// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2025 Octol1ttle <l1ttleofficial@outlook.com>
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
 */

#include "ChooseOfflineNameDialog.h"

#include <QPushButton>
#include <QRegularExpression>

#include "minecraft/auth/MinecraftAccount.h"
#include "ui_ChooseOfflineNameDialog.h"

ChooseOfflineNameDialog::ChooseOfflineNameDialog(const QString& message, QWidget* parent)
    : QDialog(parent), ui(new Ui::ChooseOfflineNameDialog)
{
    ui->setupUi(this);
    ui->label->setText(message);

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("OK"));

    const QRegularExpression usernameRegExp("^[A-Za-z0-9_]{3,16}$");
    m_usernameValidator = new QRegularExpressionValidator(usernameRegExp, this);
    ui->usernameTextBox->setValidator(m_usernameValidator);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ChooseOfflineNameDialog::~ChooseOfflineNameDialog()
{
    delete ui;
}

QString ChooseOfflineNameDialog::getUsername() const
{
    return ui->usernameTextBox->text();
}

void ChooseOfflineNameDialog::setUsername(const QString& username) const
{
    ui->usernameTextBox->setText(username);
    updateAcceptAllowed(username);
}

QString ChooseOfflineNameDialog::getUuid() const
{
    return ui->uuidTextBox->text().trimmed();
}

void ChooseOfflineNameDialog::setUuid(const QString& uuid) const
{
    ui->uuidTextBox->setText(uuid);
    updateAcceptAllowed(getUsername());
}

void ChooseOfflineNameDialog::setUuidVisible(bool visible) const
{
    ui->uuidTextBox->setVisible(visible);
    updateAcceptAllowed(getUsername());
}

void ChooseOfflineNameDialog::updateAcceptAllowed(const QString& username) const
{
    const QString uuid = getUuid();
    const bool uuidAllowed = uuid.isEmpty() || !MinecraftAccount::normalizeUuid(uuid).isEmpty();
    const bool usernameAllowed =
        ui->allowInvalidUsernames->isChecked() ? !username.isEmpty() : ui->usernameTextBox->hasAcceptableInput();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(usernameAllowed && uuidAllowed);
    ui->uuidTextBox->setToolTip(uuidAllowed ? tr("Optional. Leave empty to derive the UUID from the username.")
                                            : tr("A UUID must be 32 hexadecimal characters, optionally separated by dashes."));
}

void ChooseOfflineNameDialog::on_usernameTextBox_textEdited(const QString& newText) const
{
    updateAcceptAllowed(newText);
}

void ChooseOfflineNameDialog::on_uuidTextBox_textEdited(const QString&) const
{
    updateAcceptAllowed(getUsername());
}

void ChooseOfflineNameDialog::on_allowInvalidUsernames_checkStateChanged(const Qt::CheckState checkState) const
{
    ui->usernameTextBox->setValidator(checkState == Qt::Checked ? nullptr : m_usernameValidator);
    updateAcceptAllowed(getUsername());
}
