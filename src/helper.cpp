/*
 *  Copyright (C) 2016 Marco Martin <mart@kde.org>
 *  Copyright (C) 1998 Luca Montecchiani <m.luca@usa.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "helper.h"
#include "config-kcm.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QProcess>
#include <QMimeDatabase>

#include "kzip.h"
#include "ktar.h"
#include <KArchive>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>

ActionReply PlymouthHelper::save(const QVariantMap &args)
{
    const QString theme = args.value(QStringLiteral("theme")).toString();
    ActionReply reply;

    if (theme.isEmpty()) {
        reply = ActionReply::BackendError;
        reply.setErrorDescription(i18n("No theme specified in helper parameters."));
        return reply;
    }
    qWarning()<<"KAUTH HELPER CALLED SAVE WITH" << theme;

    {
        KConfigGroup cg(KSharedConfig::openConfig(QStringLiteral(PLYMOUTH_CONFIG_PATH)), "Daemon");
        cg.writeEntry("Theme", theme);
    }
    QFile configFile(PLYMOUTH_CONFIG_PATH);
    configFile.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ReadGroup|QFileDevice::ReadOther);


    int ret = 0;

    QProcess process;
    process.start("/usr/sbin/update-initramfs", QStringList() << "-u");
    if (!process.waitForStarted()) {
        reply = ActionReply::BackendError;
        reply.setErrorDescription(i18n("Can't start initramfs."));
        return reply;
    }
    if (!process.waitForFinished(60000)) {
        reply = ActionReply::BackendError;
        reply.setErrorDescription(i18n("Initramfs faild to run."));
        return reply;
    }
    ret = process.exitCode();

    if (ret == 0) {
        return ActionReply::SuccessReply();
    } else {
        reply = ActionReply(ActionReply::HelperErrorReply());
        reply.setErrorCode(static_cast<ActionReply::Error>(ret));
        reply.setErrorDescription(i18n("Initramfs returned with error condition %1.", ret));
        return reply;
        return reply;
    }
}

ActionReply PlymouthHelper::install(const QVariantMap &args)
{
    const QString themearchive = args.value(QStringLiteral("themearchive")).toString();

    if (themearchive.isEmpty()) {
        return ActionReply::BackendError;
    }
    qWarning()<<"KAUTH HELPER CALLED INSTALL WITH" << themearchive;

    QDir basedir(PLYMOUTH_THEMES_DIR);
    if (!basedir.exists()) {
        return ActionReply::BackendError;
    }

    // this is weird but a decompression is not a single name, so take the path instead
    QString installpath = PLYMOUTH_THEMES_DIR;
    QMimeDatabase db;
    QMimeType mimeType = db.mimeTypeForFile(themearchive);
    qWarning() << "Postinstallation: uncompress the file";

    // FIXME: check for overwriting, malicious archive entries (../foo) etc.
    // FIXME: KArchive should provide "safe mode" for this!
    QScopedPointer<KArchive> archive;

    if (mimeType.inherits(QStringLiteral("application/zip"))) {
        archive.reset(new KZip(themearchive));
    } else if (mimeType.inherits(QStringLiteral("application/tar"))
                || mimeType.inherits(QStringLiteral("application/x-gzip"))
                || mimeType.inherits(QStringLiteral("application/x-bzip"))
                || mimeType.inherits(QStringLiteral("application/x-lzma"))
                || mimeType.inherits(QStringLiteral("application/x-xz"))
                || mimeType.inherits(QStringLiteral("application/x-bzip-compressed-tar"))
                || mimeType.inherits(QStringLiteral("application/x-compressed-tar"))) {
        archive.reset(new KTar(themearchive));
    } else {
        qCritical() << "Could not determine type of archive file '" << themearchive << "'";
        return ActionReply::BackendError;
    }

    bool success = archive->open(QIODevice::ReadOnly);
    if (!success) {
        qCritical() << "Cannot open archive file '" << themearchive << "'";
        return ActionReply::BackendError;
    }

    QString themeName;
    QString themePath;
    const KArchiveDirectory *dir = archive->directory();
    //if there is more than an item in the file,
    //put contents in a subdirectory with the same name as the file
    if (dir->entries().count() > 1) {
        installpath += QLatin1Char('/') + QFileInfo(archive->fileName()).baseName();
        themeName = QFileInfo(archive->fileName()).baseName();
        themePath = installpath;
    } else {
        themeName = dir->entries().first();
        themePath = installpath + dir->entries().first();
    }
    dir->copyTo(installpath);

    archive->close();
    //QFile::remove(themearchive);
    QVariantMap map;
    map["plugin"] = themeName;
    map["path"] = themePath;
    HelperSupport::progressStep(map);
    ActionReply reply = ActionReply::SuccessReply();
    reply.setData(map);
    return reply;
}

ActionReply PlymouthHelper::uninstall(const QVariantMap &args)
{
    const QString theme = args.value(QStringLiteral("theme")).toString();

    if (theme.isEmpty()) {
        qWarning()<<"No theme specified.";
        return ActionReply::BackendError;
    }
    qWarning()<<"KAUTH HELPER CALLED UNINSTALL WITH" << theme;

    QDir dir(PLYMOUTH_THEMES_DIR);
    if (!dir.exists()) {
        qWarning()<<"Themes folder doesn't exists."<<PLYMOUTH_THEMES_DIR;
        return ActionReply::BackendError;
    }

    if (!dir.cd(theme)) {
        qWarning()<<"Theme" << theme << "doesn't exists.";
        return ActionReply::BackendError;
    }
    if (dir.removeRecursively()) {
        return ActionReply::SuccessReply();
    } else {
        return ActionReply::BackendError;
    }
}

KAUTH_HELPER_MAIN("org.kde.kcontrol.kcmplymouth", PlymouthHelper)
