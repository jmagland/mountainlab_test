/******************************************************
** See the accompanying README and LICENSE files
** Author(s): Jeremy Magland
** Created: 4/4/2016
*******************************************************/

#include "mountainprocessrunner.h"
#include <QCoreApplication>
#include <QMap>
#include <QProcess>
#include <QVariant>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "mlcommon.h"
#include "cachemanager.h"
#include "mlcommon.h"
#include "taskprogress.h"
#include <QThread>

class MountainProcessRunnerPrivate {
public:
    MountainProcessRunner* q;
    QString m_processor_name;
    QMap<QString, QVariant> m_parameters;
    //QString m_mscmdserver_url;
    QString m_mlproxy_url;
    bool m_detach;

    QString create_temporary_output_file_name(const QString& remote_url, const QString& processor_name, const QMap<QString, QVariant>& params, const QString& parameter_name);
};

MountainProcessRunner::MountainProcessRunner()
{
    d = new MountainProcessRunnerPrivate;
    d->q = this;
    d->m_detach = false;
}

MountainProcessRunner::~MountainProcessRunner()
{
    delete d;
}

void MountainProcessRunner::setProcessorName(const QString& pname)
{
    d->m_processor_name = pname;
}

QString MountainProcessRunner::makeOutputFilePath(const QString& pname)
{
    QString ret = d->create_temporary_output_file_name(d->m_mlproxy_url, d->m_processor_name, d->m_parameters, pname);
    d->m_parameters[pname] = ret;
    return ret;
}

void MountainProcessRunner::setDetach(bool val)
{
    d->m_detach = val;
}

void MountainProcessRunner::setInputParameters(const QMap<QString, QVariant>& parameters)
{
    d->m_parameters = parameters;
}

void MountainProcessRunner::setMLProxyUrl(const QString& url)
{
    d->m_mlproxy_url = url;
}

QJsonObject variantmap_to_json_obj(QVariantMap map)
{
    QJsonObject ret;
    QStringList keys = map.keys();
    // Use fromVariantMap
    foreach (QString key, keys) {
        ret[key] = QJsonValue::fromVariant(map[key]);
    }
    return ret;
}

QVariantMap json_obj_to_variantmap(QJsonObject obj)
{
    QVariantMap ret;
    QStringList keys = obj.keys();
    foreach (QString key, keys) {
        ret[key] = obj[key].toVariant();
    }
    return ret;
}

QJsonObject http_post(QString url, QJsonObject req)
{
    if (MLUtil::inGuiThread()) {
        qCritical() << "Cannot do an http_post within a gui thread: " + url;
        qCritical() << "Exiting.";
        exit(-1);
    }
    QTime timer;
    timer.start();
    QNetworkAccessManager manager;
    QNetworkRequest request;
    request.setUrl(url);
    QByteArray json_data = QJsonDocument(req).toJson();
    QNetworkReply* reply = manager.post(request, json_data);
    QEventLoop loop;
    QString ret;
    QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
        ret+=reply->readAll();
    });
    /*
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();
    */
    while (!reply->isFinished()) {
        if (MLUtil::threadInterruptRequested()) {
            qWarning() << "Halting in http_post: " + url;
            reply->abort();
            loop.quit();
        }
        loop.processEvents();
    }

    if (MLUtil::threadInterruptRequested()) {
        QJsonObject obj;
        obj["success"] = false;
        obj["error"] = "Halting in http_post: " + url;
        return obj;
    }
    else {
        printf("RECEIVED TEXT (%d ms, %d bytes) from POST %s\n", timer.elapsed(), ret.count(), url.toLatin1().data());
        QString str = ret.mid(0, 5000) + "...";
        str.replace("\\n", "\n");
        printf("%s\n", (str.toLatin1().data()));
        QJsonObject obj = QJsonDocument::fromJson(ret.toLatin1()).object();
        TaskManager::TaskProgressMonitor::globalInstance()->incrementQuantity("bytes_downloaded", ret.count());
        return obj;
    }
}

void MountainProcessRunner::runProcess()
{

    if (MLUtil::inGuiThread()) {
        qCritical() << "Cannot run mountain process in gui thread: " + d->m_processor_name;
        exit(-1);
    }

    TaskProgress task(TaskProgress::Calculate, "MS: " + d->m_processor_name);

    //if (d->m_mscmdserver_url.isEmpty()) {
    if (d->m_mlproxy_url.isEmpty()) {
        //QString mountainsort_exe = mountainlabBasePath() + "/mountainsort/bin/mountainsort";
        QString mountainprocess_exe = MLUtil::mountainlabBasePath() + "/mountainprocess/bin/mountainprocess";
        QStringList args;
        args << "run-process";
        args << d->m_processor_name;
        QStringList keys = d->m_parameters.keys();
        foreach (QString key, keys) {
            args << QString("--%1=%2").arg(key).arg(d->m_parameters.value(key).toString());
        }
        //right now we can't detach while running locally
        //if (d->m_detach) {
        //    args << QString("--_detach=1");
        //}
        task.log(QString("Executing locally: %1").arg(mountainprocess_exe));
        foreach (QString key, keys) {
            QString val = d->m_parameters[key].toString();
            task.log(QString("%1 = %2").arg(key).arg(val));
            if (val.startsWith("http")) {
                task.error("Executing locally, but parameter starts with http. Probably mlproxy url has not been set.");
                return;
            }
        }

        task.log(mountainprocess_exe + " " + args.join(" "));
        QProcess process0;
        process0.setProcessChannelMode(QProcess::MergedChannels);
        process0.start(mountainprocess_exe, args);
        if (!process0.waitForStarted()) {
            task.error("Error starting process.");
            return;
        }
        QString stdout;
        QEventLoop loop;
        while (process0.state() == QProcess::Running) {
            loop.processEvents();
            QString out = process0.readAll();
            if (!out.isEmpty()) {
                printf("%s", out.toLatin1().data());
                task.log("STDOUT: " + out);
                stdout += out;
            }
            if (MLUtil::threadInterruptRequested()) {
                task.error("Terminating due to interrupt request");
                process0.terminate();
                return;
            }
        }

        /*
        if (QProcess::execute(mountainprocess_exe, args) != 0) {
            qWarning() << "Problem running mountainprocess" << mountainprocess_exe << args;
            task.error("Problem running mountainprocess");
        }
        */
    }
    else {
        /*
        QString url = d->m_mscmdserver_url + "/?";
        url += "processor=" + d->m_processor_name + "&";
        QStringList keys = d->m_parameters.keys();
        foreach(QString key, keys)
        {
            url += QString("%1=%2&").arg(key).arg(d->m_parameters.value(key).toString());
        }
        this->setStatus("Remote " + d->m_processor_name, "MLNetwork::httpGetText: " + url, 0.5);
        MLNetwork::httpGetText(url);
        this->setStatus("", "", 1);
        */

        task.log("Setting up pp_process");
        QJsonObject pp_process;
        pp_process["processor_name"] = d->m_processor_name;
        pp_process["parameters"] = variantmap_to_json_obj(d->m_parameters);
        QJsonArray pp_processes;
        pp_processes.append(pp_process);
        QJsonObject pp;
        pp["processes"] = pp_processes;
        QString pipeline_json = QJsonDocument(pp).toJson(QJsonDocument::Compact);

        task.log(pipeline_json);

        QString script;
        script += QString("function main(params) {\n");
        script += QString("  MP.runPipeline('%1');\n").arg(pipeline_json);
        script += QString("}\n");

        QJsonObject req;
        req["action"] = "queueScript";
        req["script"] = script;
        if (d->m_detach) {
            req["detach"] = 1;
        }
        QString url = d->m_mlproxy_url + "/mpserver";
        task.log("POSTING: " + url);
        task.log(QJsonDocument(req).toJson());
        if (MLUtil::threadInterruptRequested()) {
            task.error("Halted before post.");
            return;
        }
        QTime post_timer;
        post_timer.start();
        QJsonObject resp = http_post(url, req);
        TaskManager::TaskProgressMonitor::globalInstance()->incrementQuantity("remote_processing_time", post_timer.elapsed());
        if (MLUtil::threadInterruptRequested()) {
            task.error("Halted during post: " + url);
            return;
        }
        task.log("GOT RESPONSE: ");
        task.log(QJsonDocument(resp).toJson());
        if (!resp["error"].toString().isEmpty()) {
            task.error(resp["error"].toString());
        }
    }
}

QString MountainProcessRunnerPrivate::create_temporary_output_file_name(const QString& mlproxy_url, const QString& processor_name, const QMap<QString, QVariant>& params, const QString& parameter_name)
{
    QString str = processor_name + ":";
    QStringList keys = params.keys();
    qSort(keys);
    foreach (QString key, keys) {
        str += key + "=" + params.value(key).toString() + "&";
    }

    QString file_name = QString("%1_%2.tmp").arg(MLUtil::computeSha1SumOfString(str)).arg(parameter_name);
    QString ret = CacheManager::globalInstance()->makeRemoteFile(mlproxy_url, file_name, CacheManager::LongTerm);
    return ret;
}
