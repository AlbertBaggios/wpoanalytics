/** 
 * Copyright 2011 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */ 


#ifndef CLI_H
#define CLI_H

#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QDateTime>

#include "../shared/qxtcommandoptions.h"
#include "../shared/qxtjson.h"

#include "../common/common.h"
#include "../Config/Config.h"
#include "../Parser/JSONLogParser.h"
#include "../Analytics/Analyst.h"

class CLI : public QObject {
    Q_OBJECT

public:
    CLI();
    ~CLI();

    bool start();

signals:
    // For cross-thread communication.
    void parse(QString file);
    void mine(uint from, uint to);
    void mineAndCompare(uint fromOlder, uint toOlder, uint fromNewer, uint toNewer);

    void load(QString file);
    void save(QString file);

public slots:
    // Parser.
    void wakeParser();
    void updateParsingStatus(bool parsing);
    void updateParserStats(int duration, const BatchMetadata & m);

    // Analyst: mining patterns.
    void updatePatternMiningStatus(bool miningPatterns, Time start, Time end, quint64 lines, quint64 transactions);
    void updatePatternMiningStats(int duration, Time start, Time end, quint64 pageViews, quint64 transactions, quint64 uniqueItems, quint64 frequentItems, quint64 patternTreeSize);
    void loaded(bool success, Time start, Time end, quint64 pageViews, quint64 transactions, quint64 uniqueItems, quint64 frequentItems, quint64 patternTreeSize);
    void saved(bool success);

    // Analyst: mining association rules.
    void updateRuleMiningStatus(bool mining);
    void updateRuleMiningStats(int duration, Time start, Time end, quint64 numAssociationRules, quint64 numTransactions, quint64 numLines);
    void minedRules(uint from, uint to, QList<Analytics::AssociationRule> associationRules, Analytics::SupportCount eventsInTimeRange);
    void comparedMinedRules(uint fromOlder, uint toOlder,
                            uint fromNewer, uint toNewer,
                            QList<Analytics::AssociationRule> intersectedRules,
                            QList<Analytics::AssociationRule> olderRules,
                            QList<Analytics::AssociationRule> newerRules,
                            QList<Analytics::AssociationRule> comparedRules,
                            QList<Analytics::Confidence> confidenceVariance,
                            QList<float> supportVariance,
                            QList<float> relativeSupport,
                            Analytics::SupportCount eventsInIntersectedTimeRange,
                            Analytics::SupportCount eventsInOlderTimeRange,
                            Analytics::SupportCount eventsInNewerTimeRange);

private slots:
    void patterMiningFinished();
    void startRuleMiningAfterBatch(bool lastChunkPatternMined);

private:
    // CLI functionality.
    bool parseCommandOptions();
    void run();
    void runRuleMiner();
    void verifyConfig();

    // Helpers.
    void performSave();
    void showHelpText();
    void out(const QString & module, const QString & output, int verbosity);
    void exit(int returnCode);

    // Logic.
    void reset();
    void initConfig();
    void initLogic();
    void connectLogic();
    void assignThreads();

    // CLI options.
    int optionVerbosity;
    QString optionConfigFile;
    bool optionVerifyConfig;
    bool optionInput;
    QString optionInputFile;
    bool optionInputStdin;
    bool optionLoad;
    QString optionLoadFile;
    bool optionLoadIfExists;
    bool optionSave;
    QString optionSaveFile;
    bool optionMineRules;
    bool optionMineRulesCompare;
    QPair<Bucket, Bucket> optionMineRulesRange;
    QPair<Bucket, Bucket> optionMineRulesCompareRange;
    bool optionMineRulesAfterBatch;
    bool optionOutput;
    QString optionOutputFile;
    bool optionOutputStdout;
    bool optionOutputFormatRFEJSON;
    bool optionSaveStateAfterEveryChunk;

    // Threads.
    QThread parserThread;
    QThread analystThread;

    // Core components.
    Config::Config * config;
    JSONLogParser::Parser * parser;
    Analytics::TTWDefinition * ttwDef;
    Analytics::Analyst * analyst;

    // Status.
    QMutex statusMutex;
    bool parsing;
    bool miningPatterns;
    bool miningRules;
    bool finalSave;

    // run() flow.
    bool configVerificationCompleted;
    bool loadCompleted;
    bool inputCompleted;
    bool saveCompleted;
    bool mineCompleted;

    // Stats.
    QMutex statsMutex;
    int patternTreeSize;
    Time startTime;
    Time endTime;
    Time currentBatchEndTime;
    // Parser.
    quint64 statsParserDuration;
    quint64 statsParserLines;
    quint64 statsParserLinesDropped;
    quint64 statsParserTransactions;
    double statsParserAvgTransactionLength;
    // Analyst: pattern mining.
    quint64 statsAnalystDuration;
    quint64 statsAnalystLines;
    quint64 statsAnalystTransactions;
    quint64 statsAnalystPatternTreeSize;
    quint64 statsAnalystNumFrequentItems;
    quint64 statsAnalystNumUniqueItems;
    quint64 statsAnalystLoadedLines;
    quint64 statsAnalystLoadedTransactions;
    // Analyst: rule mining.
    quint64 statsRuleMiningDuration;
    quint64 statsRuleMiningLines;
    quint64 statsRuleMiningTransactions;
    quint64 statsRuleMiningPatterns;
};

#endif // CLI_H
