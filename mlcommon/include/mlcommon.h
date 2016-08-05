/******************************************************
** See the accompanying README and LICENSE files
** Author(s): Jeremy Magland
** Created: 7/6/2016
*******************************************************/

#ifndef MLCOMMON_H
#define MLCOMMON_H

#include <QTextCodec>
#include <QDebug>
#include <QJsonValue>
#include <QByteArray>

namespace TextFile {
QString read(const QString& fname, QTextCodec* codec = 0);
bool write(const QString& fname, const QString& txt, QTextCodec* codec = 0);
};

namespace MLUtil {
QString makeRandomId(int numchars = 10);
bool threadInterruptRequested();
bool inGuiThread();
QString tempPath();
QString mountainlabBasePath();
QString mlLogPath();
QString resolvePath(const QString& basepath, const QString& path);
void mkdirIfNeeded(const QString& path);
QString computeSha1SumOfFile(const QString& path);
QString computeSha1SumOfString(const QString& str);
QList<int> stringListToIntList(const QStringList& list);
QStringList intListToStringList(const QList<int>& list);
QJsonValue toJsonValue(const QByteArray& X);
QJsonValue toJsonValue(const QList<int>& X);
QJsonValue toJsonValue(const QVector<int>& X);
QJsonValue toJsonValue(const QVector<double>& X);
void fromJsonValue(QByteArray& X, const QJsonValue& val);
void fromJsonValue(QList<int>& X, const QJsonValue& val);
void fromJsonValue(QVector<int>& X, const QJsonValue& val);
void fromJsonValue(QVector<double>& X, const QJsonValue& val);
QByteArray readByteArray(const QString& path);
bool writeByteArray(const QString& path, const QByteArray& X);
};

namespace MLCompute {
double min(const QVector<double>& X);
double max(const QVector<double>& X);
double sum(const QVector<double>& X);
double mean(const QVector<double>& X);
double stdev(const QVector<double>& X);
double norm(const QVector<double>& X);
double dotProduct(const QVector<double>& X1, const QVector<double>& X2);
double correlation(const QVector<double>& X1, const QVector<double>& X2);

template <typename T>
T max(const QVector<T>& X);
double min(long N, double* X);
double max(long N, double* X);
double sum(long N, double* X);
double mean(long N, double* X);
double dotProduct(long N, double* X1, double* X2);
double norm(long N, double* X);
double min(long N, float* X);
double max(long N, float* X);
double sum(long N, float* X);
double mean(long N, float* X);
double dotProduct(long N, float* X1, float* X2);
double norm(long N, float* X);
}

class CLParams {
public:
    CLParams(int argc, char* argv[]);
    QMap<QString, QVariant> named_parameters;
    QList<QString> unnamed_parameters;
    bool success;
    QString error_message;
};

template <typename T>
T MLCompute::max(const QVector<T>& X)
{
    return *std::max_element(X.constBegin(), X.constEnd());
}

#endif // TEXTFILE_H
