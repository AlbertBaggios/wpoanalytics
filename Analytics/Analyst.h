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


#ifndef ANALYTICS_H
#define ANALYTICS_H

#include <QObject>
#include <QTime>
#include <QList>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QPair>
#include <QThread>
#include <QWaitCondition>
#include <QMutex>

#include "../common/common.h"
#include "Item.h"
#include "TiltedTimeWindow.h"
#include "Constraints.h"
#include "FPGrowth.h"
#include "FPStream.h"
#include "RuleMiner.h"

namespace Analytics {

    class Analyst : public QObject {
        Q_OBJECT

    public:
        Analyst(const TTWDefinition & ttwDef, double minSupport, double maxSupportError, double minConfidence);
        ~Analyst();
        void setParameters(double minSupport, double maxSupportError, double minConfidence);
        void resetConstraints();
        void addFrequentItemsetItemConstraint(QSet<ItemName> items, ItemConstraintType type);
        void addRuleAntecedentItemConstraint(QSet<ItemName> items, ItemConstraintType type);
        void addRuleConsequentItemConstraint(QSet<ItemName> items, ItemConstraintType type);

        // Override moveToThread to also move the FPStream instance.
        void moveToThread(QThread * thread);

        const TTWDefinition & getTTWDefinition() const { return this->ttwDef; }
        int getPatternTreeSize() const { return this->fpstream->getPatternTreeSize(); }

        // UI integration.
        QPair<ItemName, ItemNameList> extractEpisodeFromItemset(ItemIDList itemset) const;
        ItemNameList itemsetIDsToNames(ItemIDList itemset) const;

    signals:
        // Signals for UI.
        void analyzing(bool analyzing, Time start, Time end, quint64 pageViews, quint64 transactions);
        void stats(int duration, Time start, Time end, quint64 pageViews, quint64 transactions, quint64 uniqueItems, quint64 frequentItems, quint64 patternTreeSize);
        void mining(bool);
        void ruleMiningStats(int duration, Time start, Time end, quint64 numAssociationRules, quint64 numTransactions, quint64 numLines);
        void loaded(bool success, Time start, Time end, quint64 pageViews, quint64 transactions, quint64 uniqueItems, quint64 frequentItems, quint64 patternTreeSize);
        void saved(bool success);
        void newItemsEncountered(Analytics::ItemIDNameHash itemIDNameHash);

        // Signals for calculations.
        void processedChunkOfBatch(bool batchCompleted);
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

    public slots:
        void analyzeChunkOfBatch(Batch<RawTransaction> chunk);
        void mineRules(uint from, uint to);
        void mineAndCompareRules(uint fromOlder, uint toOlder, uint fromNewer, uint toNewer);
        void load(QString fileName);
        void save(QString fileName);

    protected slots:
        void fpstreamProcessedChunkOfBatch();

    protected:
        TTWDefinition ttwDef;
        FPStream * fpstream;
        double minSupport;
        double maxSupportError;
        double minConfidence;
        quint32 currentQuarterID;
        bool isLastChunk;

        Constraints frequentItemsetItemConstraints;
        Constraints ruleAntecedentItemConstraints;
        Constraints ruleConsequentItemConstraints;

        ItemIDNameHash itemIDNameHash;
        ItemNameIDHash itemNameIDHash;
        ItemIDList sortedFrequentItemIDs;

        // Stats for the UI.
        uint currentBatchStartTime;
        uint currentBatchEndTime;
        quint64 currentBatchNumPageViews;
        quint64 currentBatchNumTransactions;
        uint allBatchesStartTime;
        uint allBatchesEverStartTime; // TODO: this one is not yet being used.
        quint64 allBatchesNumPageViews;
        quint64 allBatchesNumTransactions;
        QTime timer;

        int uniqueItemsBeforeMining;
    };
}

#endif // ANALYST_H
