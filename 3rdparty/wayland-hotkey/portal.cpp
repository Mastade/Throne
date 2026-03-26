/**
 * Copyright © 2017-2026 Wellington Wallace
 *
 * This file is part of Easy Effects.
 *
 * Easy Effects is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Easy Effects is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Easy Effects. If not, see <https://www.gnu.org/licenses/>.
 */

#include "portal.h"
#include <qcontainerfwd.h>
#include <qdbusextratypes.h>
#include <qdbuspendingcall.h>
#include <qdbuspendingreply.h>
#include <qlocalsocket.h>
#include <qlogging.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusReply>
#include <format>
#include <utility>
#include <qrandom.h>

// Based on https://github.com/SourceReviver/qt_wayland_globalshortcut_via_portal/blob/main/wayland_shortcut.cpp
// Documentation: https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html

GlobalShortcuts::GlobalShortcuts(QObject* parent) : QObject(parent) {
  qDBusRegisterMetaType<std::pair<QString, QVariantMap>>();
  qDBusRegisterMetaType<QList<QPair<QString, QVariantMap>>>();

  QMap<QString, QVariant> options;
  options["handle_token"] = QString("easyeffects%1").arg("todo"); //todo
  options["session_handle_token"] = session_handle_token;

  QList<QVariant> args_create_session;

  args_create_session.append(options);

  QDBusMessage create_session =
      QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                     "org.freedesktop.portal.GlobalShortcuts", "CreateSession");

  create_session.setArguments(args_create_session);

  auto message = QDBusConnection::sessionBus().call(create_session);

  response_handle = message.arguments().first().value<QDBusObjectPath>();

  QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", response_handle.path(),
                                        "org.freedesktop.portal.Request", "Response", this,
                                        SLOT(onSessionCreatedResponse(uint, QVariantMap)));
}

void GlobalShortcuts::onSessionCreatedResponse(uint responseCode, const QVariantMap& results) {
  if (responseCode != 0) {
    qWarning() << (
        std::format("D-Bus CreateSession for GlobalShortcuts was denied or failed. Response code: {}", responseCode));

    return;
  }

  if (!results.contains("session_handle")) {
    qWarning() << ("Missing session_handle on GlobalShortcuts CreateSession response.");

    return;
  }

  session_obj_path = QDBusObjectPath(results.value("session_handle").value<QString>());

  QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", response_handle.path(),
                                           "org.freedesktop.portal.Request", "Response", this,
                                           SLOT(onSessionCreatedResponse(uint, QVariantMap)));

  qDebug() << ("D-Bus session for GlobalShortcuts created.");

  QDBusConnection::sessionBus().connect(
      "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.GlobalShortcuts",
      "Activated", this, SLOT(process_activated_signal(QDBusObjectPath, QString, qulonglong, QVariantMap)));
}

void GlobalShortcuts::process_activated_signal([[maybe_unused]] const QDBusObjectPath& session_handle,
                                               const QString& shortcut_id,
                                               [[maybe_unused]] qulonglong timestamp,
                                               [[maybe_unused]] const QVariantMap& options) {
  // qDebug() << "Got GlobalShortcuts Activated Signal ->" << session_handle.path() << shortcut_id << timestamp <<
  // options;

  Q_EMIT hotkeyActivated(shortcut_id);
}

 bool GlobalShortcuts::bind_shortcuts(const QList<std::shared_ptr<GlobalShortcutData>>& shortcutData) {
  // For security reasons, it's better to show the session handle only in development/debug mode.
  // util::info("Session handle object response:" + session_obj_path.path().toStdString());

  // a(sa{sv})
  QList<QPair<QString, QVariantMap>> shortcutList;

  for (const auto& shortcutD : shortcutData) {
    QPair<QString, QVariantMap> shortcut;

    QVariantMap shortcut_options;
    shortcut.first = shortcutD->shortcut_id;
    shortcut_options.insert("description", shortcutD->shortcut_id);
    shortcut_options.insert("preferred_trigger", shortcutD->preferred_trigger);
    shortcut.second = shortcut_options;

    shortcutList << shortcut;
  }

  QMap<QString, QVariant> bind_opts;

  bind_opts.insert("handle_token", QString("throne%1").arg(QRandomGenerator::global()->generate()));

  QList<QVariant> bind_shortcut_args;

  /**
   * 1. session handle object
   * 2. shortcuts list
   * 3. window identifier (https://flatpak.github.io/xdg-desktop-portal/docs/window-identifiers.html)
   * 4. options (contains request handle token)
   */
  bind_shortcut_args.append(session_obj_path);
  bind_shortcut_args.append(QVariant::fromValue(shortcutList));
  bind_shortcut_args.append(QString());  // can be empty
  bind_shortcut_args.append(bind_opts);

  QDBusMessage bind_shortcut =
      QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                     "org.freedesktop.portal.GlobalShortcuts", "BindShortcuts");

  bind_shortcut.setArguments(bind_shortcut_args);

  // qDebug() << "input of bind->" << bind_shortcut.arguments();

  QDBusMessage bind_ret = QDBusConnection::sessionBus().call(bind_shortcut);

  // qDebug() << "GlobalShortcuts BindShortcuts response ->" << bind_ret;

  if (bind_ret.type() == QDBusMessage::ErrorMessage) {
    return false;
  } else {
    return true;
  }
}