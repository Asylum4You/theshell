/****************************************
 *
 *   theShell - Desktop Environment
 *   Copyright (C) 2018 Victor Tran
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *************************************/

#include "notificationappgroup.h"
#include "ui_notificationappgroup.h"

extern float getDPIScaling();

NotificationAppGroup::NotificationAppGroup(QString appIdentifier, QIcon appIcon, QString appName, QWidget *parent) :
    QFrame(parent),
    ui(new Ui::NotificationAppGroup)
{
    ui->setupUi(this);

    this->appIcon = appIcon;
    this->appIdentifier = appIdentifier;

    ui->appIcon->setPixmap(appIcon.pixmap(24 * getDPIScaling(), 24 * getDPIScaling()));
    ui->appName->setText(appName);

    //this->setFixedHeight(0);
}

NotificationAppGroup::~NotificationAppGroup()
{
    delete ui;
}

QString NotificationAppGroup::getIdentifier() {
    return this->appIdentifier;
}

void NotificationAppGroup::AddNotification(NotificationObject *object) {
    NotificationPanel* panel = new NotificationPanel(object);
    ((QBoxLayout*) ui->notificationsWidget->layout())->addWidget(panel);

    connect(panel, &NotificationPanel::dismissed, [=] {
        notifications.removeAll(panel);

        if (notifications.count() > 4) {
            notifications.at(4)->expandHide();
        }

        if (notifications.count() == 0) {
            tVariantAnimation* anim = new tVariantAnimation();
            anim->setStartValue(this->height());
            anim->setEndValue(0);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->setDuration(500);
            connect(anim, SIGNAL(finished()), this, SLOT(deleteLater()));
            connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
            connect(anim, &tVariantAnimation::valueChanged, [=](QVariant value) {
                this->setFixedHeight(value.toInt());
            });
            anim->start();
        }
        updateCollapsedCounter();
    });

    QTimer::singleShot(100, [=] {
        if (notifications.count() <= 5 || expanded) {
            panel->expandHide();
        }

        tVariantAnimation* anim = new tVariantAnimation();
        anim->setStartValue(this->height());
        anim->setEndValue(this->sizeHint().height());
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setDuration(500);
        connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
        connect(anim, &tVariantAnimation::valueChanged, [=](QVariant value) {
            this->setFixedHeight(value.toInt());
        });
        connect(anim, &tVariantAnimation::finished, [=] {
            //Remove layout constraints
            this->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        });
        anim->start();
    });

    notifications.append(panel);

    updateCollapsedCounter();
}

int NotificationAppGroup::count() {
    return notifications.count();
}


void NotificationAppGroup::on_closeAllNotificationsButton_clicked()
{
    this->clearAll();
}

void NotificationAppGroup::clearAll() {
    while (!notifications.isEmpty()) {
        notifications.takeFirst()->getObject()->dismiss();
    }
}


void NotificationAppGroup::updateCollapsedCounter() {
    if (notifications.count() > 5) {
        if (expanded) {
            ui->collapsedLabel->setText(tr("Collapse Notifications"));
            ui->expandNotificationsButton->setIcon(QIcon::fromTheme("go-up"));
        } else {
            ui->collapsedLabel->setText(tr("+%n notifications collapsed", "", notifications.count() - 5));
            ui->expandNotificationsButton->setIcon(QIcon::fromTheme("go-down"));
        }
        ui->collapsedFrame->setVisible(true);
        ui->expandNotificationsButton->setVisible(true);
    } else {
        ui->collapsedFrame->setVisible(false);
        ui->expandNotificationsButton->setVisible(false);
    }
    emit notificationCountChanged();
}

void NotificationAppGroup::on_collapsedLabel_clicked()
{
    ui->expandNotificationsButton->click();
}

void NotificationAppGroup::on_expandNotificationsButton_clicked()
{
    expanded = !expanded;
    if (expanded) {
        for (NotificationPanel* panel : notifications) {
            panel->expandHide();
        }
    } else {
        for (int i = 5; i < notifications.count(); i++) {
            notifications.at(i)->collapseHide();
        }
    }
    updateCollapsedCounter();
}
