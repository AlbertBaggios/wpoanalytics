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


#include "JSONLogParser.h"

namespace JSONLogParser {
    Config::EpisodeNameIDHash Parser::episodeNameIDHash;
    Config::EpisodeIDNameHash Parser::episodeIDNameHash;
    QHash<Config::EpisodeName, QString> Parser::episodeNameFieldNameHash;

    QMutex Parser::episodeHashMutex;

    Parser::Parser(Config::Config config, uint secPerBatch) {
        this->secPerBatch = secPerBatch;
        this->config = config;
    }


    //---------------------------------------------------------------------------
    // Public static methods.


    /**
     * Map a sample (raw string) to a Sample data structure.
     *
     * @param sample
     *   Raw sample, a JSON object with 'int', 'normal', 'denorm' keys, each of
     *   which maps to an array of single-level objects.
     * @return
     *   Corresponding Sample data structure.
     */
    Config::Sample Parser::parseSample(const QString & rawSample, const Config::Config * const config) {
        Config::Sample sample;
        Analytics::Constraints categoricalItemConstraints;
        Analytics::Constraints numericalItemConstraints;

        // Don't waste time parsing checkpointing lines.
        if (rawSample.at(0) == '#')
            return sample; // Samples without circumstances aren't accepted.

        // Get config.
        categoricalItemConstraints.setItemConstraints(config->getParserCategoricalItemConstraints());
        numericalItemConstraints.setItemConstraints(config->getParserNumericalItemConstraints());
        QHash<Config::EpisodeName, Config::Attribute> numericalAttributes = config->getNumericalAttributes();
        QHash<Config::EpisodeName, Config::Attribute> categoricalAttributes = config->getCategoricalAttributes();
        Config::Attribute attribute;

        // Parse.
        QVariantMap json = QxtJSON::parse(rawSample).toMap();

        // 1) Process normals.
        const QVariantMap & normals = json["normal"].toMap();
        QString value;
        foreach (const QString & key, normals.keys()) {
            if (categoricalAttributes.contains(key)) {
                attribute = categoricalAttributes[key];

                value = normals[key].toString();

                // parentAttribute: only insert a circumstance if the parent
                // attribute also exists.
                if (!attribute.parentAttribute.isNull()) {
                    if (normals.contains(attribute.parentAttribute)) {
                        sample.circumstances.insert(
                            attribute.name +
                            ':' +
                            normals[attribute.parentAttribute].toString() +
                            ':' +
                            value
                        );
                    }
                    else {
                        qWarning(
                            "A sample did NOT contain the parent attribute '%s' for the attribute '%s'!",
                            qPrintable(attribute.parentAttribute),
                            qPrintable(attribute.name)
                        );
                    }
                }
                // hierarchySeparator: insert multiple items if the hierarchy
                // separator does exist, otherwise just insert the circumstance
                // itself.
                else if (!attribute.hierarchySeparator.isNull() && value.contains(attribute.hierarchySeparator)) {
                    // Insert all sections.
                    for (int i = 0; i < value.count(attribute.hierarchySeparator, Qt::CaseSensitive); i++) {
                        sample.circumstances.insert(
                            attribute.name +
                            value.section(attribute.hierarchySeparator, 0, i)
                        );
                    }
                    // Insert the whole thing, too.
                    sample.circumstances.insert(attribute.name + ':' + value);
                }
                else
                    sample.circumstances.insert(attribute.name + ':' + value);
            }
        }

        // 2) Process denormals.
        const QVariantMap & denorms = json["denorm"].toMap();
        foreach (const QString & key, denorms.keys()) {
            if (categoricalAttributes.contains(key)) {
                attribute = categoricalAttributes[key];
                sample.circumstances.insert(attribute.name + ":" + denorms[key].toString());
            }
        }

        // If the sample doesn't match the constraints, clear the circumstances
        // and return that right away. (Samples without circumstances are
        // discarded.)
        if (!categoricalItemConstraints.matchItemset(sample.circumstances)) {
            sample.circumstances.clear();
            return sample;
        }

        Config::Circumstances categoricalCircumstances = sample.circumstances;

        // 3) Process ints.
        const QVariantMap & integers = json["int"].toMap();
        Config::Episode episode;
        Config::EpisodeSpeed speed;
        foreach (const QString & key, integers.keys()) {
            if (numericalAttributes.contains(key)) {
                attribute = numericalAttributes[key];

                if (attribute.isEpisode) {
                    episode.id = Parser::mapEpisodeNameToID(attribute.name, attribute.field);
                    episode.duration = integers[key].toInt();
#ifdef DEBUG
                    episode.IDNameHash = &Parser::episodeIDNameHash;
#endif
                    // Drop the episode when no data was collected.
                    if (episode.duration > 0)
                        sample.episodes.append(episode);
                }
                else {
                    // Discretize based on the circumstances gathered from the
                    // categorical attributes. This ensures that all numerical
                    // attributes that follow this code path will receive an
                    // identical set of circumstances.
                    speed = config->discretize(attribute.field, integers[key].toInt(), categoricalCircumstances);
                    sample.circumstances.insert(attribute.name + ":" + speed);
                }
            }
        }

        // If the sample doesn't match the constraints, clear the circumstances
        // and return that right away. (Samples without circumstances are
        // discarded.)
        if (!numericalItemConstraints.matchItemset(sample.circumstances)) {
            sample.circumstances.clear();
            return sample;
        }

        // One special case: time.
        sample.time = integers["time"].toInt();

        return sample;
    }


    //---------------------------------------------------------------------------
    // Public slots.

    /**
     * Parse the given Episodes log file.
     *
     * Emits a signal for every chunk (with PARSE_CHUNK_SIZE lines). This signal
     * can then be used to process that chunk. This allows multiple chunks to be
     * processed in parallel (by using QtConcurrent), if that is desired.
     *
     * @param fileName
     *   The full path to an Episodes log file.
     */
    void Parser::parse(const QString & fileName) {
        // Notify the UI.
        emit parsing(true);

        QFile file;
        QString line;
        QStringList chunk;
        int numLines = 0;
        WindowMarkerMethod markerMethod = this->getWindowMarkerMethod();
        QString timeWindowMarkerLine = this->getWindowMarkerLine();
        bool opened = false;

        if (fileName == ":stdin")
            opened = file.open(stdin, QIODevice::ReadOnly | QIODevice::Text);
        else {
            file.setFileName(fileName);
            opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
        }

        if (!opened) {
            qDebug() << "Could not open file for reading.";
            // TODO: emit signal indicating parsing failure.
            return;
        }
        else {
            this->timer.start();
            QTextStream in(&file);
            while (!in.atEnd()) {
                line = in.readLine();
                numLines++;

                // Special handling for marker lines.
                if (markerMethod == WindowMarkerMethodMarkerLine) {
                    if (line == timeWindowMarkerLine) {
                        this->processParsedChunk(chunk, true);
                        chunk.clear();
                    }
                    else
                        chunk.append(line);
                }
                // Append *all* lines if there are no marker lines.
                else
                    chunk.append(line);

                // Always process parsed chunks if we've reached the max chunk
                // size.
                if (chunk.size() == PARSE_CHUNK_SIZE) {
                    this->processParsedChunk(chunk, false);
                    chunk.clear();
                }
            }

            // Check if we have another chunk (with size < PARSE_CHUNK_SIZE).
            if (chunk.size() > 0) {
                // TODO: remove the forceProcessing = true parameter!
                this->processParsedChunk(chunk, false, true);
            }

            // Notify the UI.
            emit parsing(false);
        }
    }

    void Parser::continueParsing() {
        QMutexLocker(&this->mutex);
        this->condition.wakeOne();
    }


    //---------------------------------------------------------------------------
    // Protected slots.

    void Parser::processChunkOfBatch(const Batch<Config::Sample> & s) {
        Batch<RawTransaction> b;
        b.meta           = s.meta;
        b.meta.samples   = s.data.size();
        b.meta.startTime = s.data.first().time;
        b.meta.endTime   = s.data.last().time;

        // Map: samples to groups of transactions.
        QList< QList<RawTransaction> > groupedTransactions;
        SampleToTransactionMapper mapper(&this->config);
        groupedTransactions = QtConcurrent::blockingMapped(s.data, mapper);

        // Reduce: merge transaction groups into a single list of transactions.
        b.meta.items = 0;
        QList<RawTransaction> transactionGroup;
        foreach (transactionGroup, groupedTransactions) {
            b.data.append(transactionGroup);
            foreach (const QStringList & transaction, transactionGroup)
                b.meta.items += transaction.size();
        }
        b.meta.transactions          = b.data.size();
        b.meta.transactionsPerSample = 1.0 * b.meta.transactions/b.meta.samples;
        b.meta.itemsPerTransaction   = 1.0 * b.meta.items/b.meta.transactions;

        emit stats(timer.elapsed(), b.meta);
        emit parsedChunkOfBatch(b);

        // Pause the parsing until these transactions have been processed!
        this->mutex.lock();
        this->condition.wait(&this->mutex);
        this->timer.start(); // Restart the timer.
        this->mutex.unlock();
    }


    //---------------------------------------------------------------------------
    // Protected overridable methods.

    WindowMarkerMethod Parser::getWindowMarkerMethod() const {
        return WindowMarkerMethodTimestamp;
    }

    QString Parser::getWindowMarkerLine() const {
        return QString::null;
    }

    void Parser::processParsedChunk(const QStringList & chunk,
                                    bool finishesTimeWindow,
                                    bool forceProcessing)
    {
        static quint32 batchID = 0;
        static Batch<Config::Sample> b;
        static WindowMarkerMethod markerMethod = this->getWindowMarkerMethod();
        static quint32 discardedSamples = 0;
        quint16 sampleNumber = 0;


        // Perform the mapping from strings to EpisodesLogLine concurrently.
        QList<Config::Sample> samples;
        ParseSampleMapper mapper(&this->config);
        samples = QtConcurrent::blockingMapped(chunk, mapper);

        Config::Sample sample;
        foreach (sample, samples) {
            // Discard samples without circumstances.
            if (sample.circumstances.isEmpty()) {
                discardedSamples++;
                continue;
            }

            sampleNumber++;

            if (markerMethod == WindowMarkerMethodTimestamp) {
                // Calculate the initial currentBatchID.
                if (batchID == 0)
                    batchID = Parser::calculateBatchID(sample.time);

                // Create a batch (every secPerBatch seconds) and process it.
                if (sampleNumber % CHECK_TIME_INTERVAL == 0) {
                    sampleNumber = 0; // Reset.
                    quint32 newBatchID = Parser::calculateBatchID(sample.time);
                    if (newBatchID > batchID && !b.data.isEmpty()) {
                        b.meta.setChunkInfo(batchID, true, discardedSamples);
                        this->processChunkOfBatch(b);

                        batchID = newBatchID;
                        discardedSamples = 0;
                        b.data.clear();
                    }
                }
            }

            // Ensure that the batch doesn't get too large (and thus consumes
            // too much memory): let it be processed when it has grown to
            // PROCESS_CHUNK_SIZE lines.
            if (b.data.size() == PROCESS_CHUNK_SIZE) {
                b.meta.setChunkInfo(batchID, false, discardedSamples);
                this->processChunkOfBatch(b);

                discardedSamples = 0;
                b.data.clear();
            }

            b.data.append(sample);
        }

        if ((finishesTimeWindow || forceProcessing) && !b.data.isEmpty()) {
            b.meta.setChunkInfo(batchID, true, discardedSamples);
            this->processChunkOfBatch(b);

            discardedSamples = 0;
            b.data.clear();
        }

        if (finishesTimeWindow)
            batchID++;
    }


    //---------------------------------------------------------------------------
    // Protected methods.

    quint32 Parser::calculateBatchID(Time t) {
        static quint32 minBatchID = 0;

        quint32 batchID;

        batchID = t / this->secPerBatch;

        // Cope with data that is not chronologically ordered perfectly.
        if (batchID > minBatchID)
            minBatchID = batchID;
        else if (minBatchID > batchID)
            batchID = minBatchID;

        return batchID;
    }

    /**
     * Map an episode name to an episode ID. Generate a new ID when necessary.
     *
     * @param name
     *   Episode name.
     * @param fieldName
     *   The name of the field; this may differ from the episode name sometimes.
     * @return
     *   The corresponding episode ID.
     *
     * Modifies a class variable upon some calls (i.e. when a new key must be
     * inserted in the QHash), hence we need to use a mutex to ensure thread
     * safety.
     */
    Config::EpisodeID Parser::mapEpisodeNameToID(const Config::EpisodeName & name, const QString & fieldName) {
        if (!Parser::episodeNameIDHash.contains(name)) {
            Parser::episodeHashMutex.lock();

            Config::EpisodeID id = Parser::episodeNameIDHash.size();
            Parser::episodeNameIDHash.insert(name, id);
            Parser::episodeIDNameHash.insert(id, name);

            Parser::episodeNameFieldNameHash.insert(name, fieldName);

            Parser::episodeHashMutex.unlock();
        }

        return Parser::episodeNameIDHash[name];
    }

    QList<QStringList> Parser::mapSampleToTransactions(const Config::Sample & sample, const Config::Config * const config) {
        QList<RawTransaction> transactions;

        Config::Episode episode;
        Config::EpisodeName episodeName;
        RawTransaction transaction;
        foreach (episode, sample.episodes) {
            episodeName = Parser::episodeIDNameHash[episode.id];
            transaction << QString("episode:") + episodeName
                        << QString("duration:") + config->discretize(
                               Parser::episodeNameFieldNameHash[episodeName],
                               episode.duration,
                               sample.circumstances
                           )
                        // Append the circumstances.
                        << sample.circumstances.toList();
            transactions << transaction;
            transaction.clear();
        }

        // Only a single transaction if there are no episodes!
        if (sample.episodes.isEmpty()) {
            transaction << sample.circumstances.toList();
            transactions << transaction;
        }

        return transactions;
    }
}
