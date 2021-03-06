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


#ifndef TTWDEFINITION
#define TTWDEFINITION

#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QDebug>

#include "../common/common.h"

namespace Analytics {
    class TTWDefinition {
    public:
        TTWDefinition() {}
        TTWDefinition(uint secPerWindow, QMap<char, uint> granularities, QList<char> order);

        // Operators.
        friend bool operator==(const TTWDefinition & def1, const TTWDefinition & def2);
        friend bool operator!=(const TTWDefinition & def1, const TTWDefinition & def2);

        // Getters.
        uint getSecPerWindow() const { return this->secPerWindow; }

        // Queries.
        bool exists(Bucket b) const;
        bool bucketIsBeforeGranularity(Bucket b, Granularity g) const;
        Granularity findLowestGranularityAfterBucket(Bucket b) const;
        uint secondsToBucket(Bucket bucket, bool includeBucketItself) const;
        uint timeOfNextBucket(uint time) const;

        // (De)serialization helper methods.
        QString serialize() const;
        bool deserialize(QString serialized);

        uint secPerWindow;
        int numGranularities;
        int numBuckets;
        QVector<Bucket> bucketCount;
        QVector<Bucket> bucketOffset;
        QVector<char> granularityChar;

    protected:
        void reset();
    };

#ifdef DEBUG
    QDebug operator<<(QDebug dbg, const TTWDefinition & def);
#endif
}

#endif // TTWDEFINITION
