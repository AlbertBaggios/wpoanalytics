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


#include "PatternTree.h"

namespace Analytics {

    //------------------------------------------------------------------------
    // Public methods.

    PatternTree::PatternTree() {
        this->root = new FPNode<TiltedTimeWindow>(ROOT_ITEMID);
        this->nodeCount = 0;
        this->currentQuarter = 0;
    }

    PatternTree::~PatternTree() {
        delete root;
    }

    /**
     * Serialize *all* patterns in the tree to a writable stream; one node per
     * line.
     */
    bool PatternTree::serialize(QTextStream & output,
                                            const ItemIDNameHash & itemIDNameHash) const
    {
        // First line: PatternTree metadata.
        QVariantMap json;
        json.insert("v", 2);
        json.insert("currentQuarter", (int) currentQuarter);
        json.insert("tilted time window definition", this->ttwDef.serialize());

        output << QxtJSON::stringify(json) << "\n";

        // Remaining lines: nodes in PatternTree.
        PatternTree::recursiveSerializer(this->root, itemIDNameHash, output, QList<ItemName>());

        // TODO: add JSON conversion error checks.

        return true;
    }

    /**
     * Deserialize *one* pattern from a readable stream (i.e. only one node,
     * thus one line).
     */
    bool PatternTree::deserialize(QTextStream & input,
                                                const ItemIDNameHash * itemIDNameHash,
                                                const ItemNameIDHash & itemNameIDHash,
                                                quint32 updateID)
    {
        QVariantMap json;

        // First line: PatternTree metadata.
        json = QxtJSON::parse(input.readLine()).toMap();

        int version = json["v"].toInt();
        if (version == 1 || version == 2) {
            // First line: PatternTree metadata.
            // Don't store "currentQuarter" right away, or it would interfere
            // with addPattern() calls. Set it after all patterns have been
            // loaded.
            uint futureCurrentQuarter = json["currentQuarter"].toInt();
            // version 2 addition: tilted time window definition
            if (json.keys().contains("tilted time window definition")) {
                if (!this->ttwDef.deserialize(json["tilted time window definition"].toString()))
                    return false;
            }
            else {
                // Before version 2, a different default was used, so set that
                // default.
                this->ttwDef.deserialize("900:QQQQHHHHHHHHHHHHHHHHHHHHHHHHDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDMMMMMMMMMMMMY");
            }

            // Rebuild the root node.
            this->root->getPointerToValue()->build(this->ttwDef, true);

            // Remaining lines: nodes in PatternTree.
            TiltedTimeWindow * ttw;
            QList<QVariant> patternVariant;
            FrequentItemset frequentItemset;
#ifdef DEBUG
            frequentItemset.IDNameHash = itemIDNameHash;
#else
            Q_UNUSED(itemIDNameHash)
#endif
            ItemID itemID;
            while (!input.atEnd()) {
                // Parse a line.
                json = QxtJSON::parse(input.readLine()).toMap();

                // Store pattern (which is a frequent itemset) in PatternTree.
                patternVariant = json["pattern"].toList();
                frequentItemset.itemset.clear();
                foreach (const QVariant & variant, patternVariant) {
                    itemID = itemNameIDHash[(ItemName) variant.toString()];
                    frequentItemset.itemset.append(itemID);
                }
                this->addPattern(frequentItemset, updateID);

                // Update the TiltedTimeWindow for the pattern we just stored.
                ttw = this->getPatternSupport(frequentItemset.itemset);
                ttw->fromVariantMap(json["tilted time window"].toMap());
            }

            // Set "currentQuarter".
            this->currentQuarter = futureCurrentQuarter;
        }

        // TODO: add JSON conversion error checks.

        return true;
    }

    TiltedTimeWindow * PatternTree::getPatternSupport(const ItemIDList & pattern) const {
        return this->root->findNodeByPattern(pattern);
    }

    /**
     * Get the frequent itemsets that match given constraints for a range of
     * buckets in the TiltedTimeWindows in this PatternTree.
     *
     * @param minSupport
     *   The minimum support that the itemset must have over the given range
     *   to qualify as "frequent".
     * @param frequentItemsetConstraints
     *   The constraints that frequent itemsets must match.
     * @param from
     *   The range starts at this bucket.
     * @param to
     *   The range starts at this bucket.
     * @param prefix
     *   Internal parameter (for recursive calls).
     * @param node
     *   Internal parameter (for recursive calls).
     * @return
     *   The frequent itemsets over the given range that match the given
     *   constraints.
     */
    QList<FrequentItemset> PatternTree::getFrequentItemsetsForRange(SupportCount minSupport, const Constraints & frequentItemsetConstraints, uint from, uint to, const ItemIDList & prefix, const FPNode<TiltedTimeWindow> * node) const {
        QList<FrequentItemset> frequentItemsets;
        FrequentItemset frequentItemset;

        // Start at the root.
        if (node == NULL)
            node = this->root;
        // If it's not the root node, set the current frequent itemset.
        else {
            frequentItemset.itemset = prefix;
            frequentItemset.itemset.append(node->getItemID());
            frequentItemset.support = node->getValue().getSupportForRange(from, to);
#ifdef DEBUG
            frequentItemset.IDNameHash = node->itemIDNameHash;
#endif
        }

        // Add this frequent itemset to the list of frequent itemsets if
        // it qualifies through its support and if it matches the
        // constraints.
        if (frequentItemset.support > minSupport && frequentItemsetConstraints.matchItemset(frequentItemset.itemset))
            frequentItemsets.append(frequentItemset);

        // Recursive call for each child node of the current node.
        foreach (const FPNode<TiltedTimeWindow> * child, node->getChildren()) {
            frequentItemsets.append(this->getFrequentItemsetsForRange(
                    minSupport,
                    frequentItemsetConstraints,
                    from,
                    to,
                    frequentItemset.itemset,
                    child
            ));
        }

        return frequentItemsets;
    }

    /**
     * Get the total support of all frequent itemsets that match given
     * constraints for a range of buckets in the TiltedTimeWindows in this
     * PatternTree.
     *
     * @param c
     *   The constraints that frequent itemsets/patterns  must match.
     * @param from
     *   The range starts at this bucket.
     * @param to
     *   The range starts at this bucket.
     * @return
     *   The total support of all frequent itemsets that match the given
     *   constraints.
     */
    SupportCount PatternTree::getTotalSupportForRange(const Constraints & c,
                                                      uint from, uint to) const
    {
        uint totalSupport = 0;
        PatternTree::totalSupportForRangeHelper(c, from, to, totalSupport,
                                                ItemIDList(), this->root);

        return totalSupport;
    }

    void PatternTree::addPattern(const FrequentItemset & pattern, quint32 updateID) {
        // The initial current node is the root node.
        FPNode<TiltedTimeWindow> * currentNode = root;
        FPNode<TiltedTimeWindow> * nextNode;

        foreach (ItemID itemID, pattern.itemset) {
            if (currentNode->hasChild(itemID))
                nextNode = currentNode->getChild(itemID);
            else {
                // Create a new node and add it as a child of the current node.
                nextNode = new FPNode<TiltedTimeWindow>(itemID);
                nextNode->getPointerToValue()->build(this->ttwDef);
                this->nodeCount++;
                nextNode->setParent(currentNode);
#ifdef DEBUG
        nextNode->itemIDNameHash = pattern.IDNameHash;
#endif
            }

            // We've processed this item in the transaction, time to move on
            // to the next!
            currentNode = nextNode;
            nextNode = NULL;
        }

        TiltedTimeWindow * ttw = currentNode->getPointerToValue();

        // Make sure the quarters are in sync.
        for (uint i = ttw->getCapacityUsed((Granularity) 0); i < this->currentQuarter; i++)
            ttw->append(0, 0);

        // Now that the quarters are in sync, finally append the quarter.
        ttw->append(pattern.support, updateID);
    }

    void PatternTree::removePattern(FPNode<TiltedTimeWindow> * const node) {
        this->nodeCount -= (1 + node->getNumDescendants());
        delete node;
    }


    //------------------------------------------------------------------------
    // Static public methods.

    ItemIDList PatternTree::getPatternForNode(FPNode<TiltedTimeWindow> const * const node) {
        ItemIDList pattern;
        FPNode<TiltedTimeWindow> const * nextNode;

        nextNode = node;
        while (nextNode->getItemID() != ROOT_ITEMID) {
            pattern.prepend(nextNode->getItemID());
            nextNode = nextNode->getParent();
        }

        return pattern;
    }


    //------------------------------------------------------------------------
    // Static protected methods.

    void PatternTree::recursiveSerializer(const FPNode<TiltedTimeWindow> * node,
                                          const ItemIDNameHash & itemIDNameHash,
                                          QTextStream & output,
                                          QList<ItemName> pattern)
    {
        ItemID itemID = node->getItemID();
        if (itemID != ROOT_ITEMID) {
            // Update pattern.
            pattern << itemIDNameHash[itemID];

            // Convert pattern to a variant.
            QList<QVariant> patternVariant;
            foreach (const ItemName & name, pattern)
                patternVariant << QVariant(name);

            // Build a variant map, which we can then convert to JSON.
            QVariantMap v;
            v.insert("pattern", patternVariant);
            v.insert("tilted time window", node->getValue().toVariantMap());
            output << QxtJSON::stringify(v) << "\n";
        }

        // Recurse.
        if (node->numChildren() > 0)
            foreach (const FPNode<TiltedTimeWindow> * child, node->getChildren())
                PatternTree::recursiveSerializer(child, itemIDNameHash, output, pattern);
    }

    /**
     * Helper method for @func PatternTree::gettotalSupportForRange().
     *
     * @param c
     *   The constraints that frequent itemsets/patterns  must match.
     * @param from
     *   The range starts at this bucket.
     * @param to
     *   The range starts at this bucket.
     * @param totalSupport
     *   Internal parameter (for recursive calls).
     * @param prefix
     *   Internal parameter (for recursive calls).
     * @param node
     *   Internal parameter (for recursive calls).
     * @return
     *   The total support of all frequent itemsets that match the given
     *   constraints.
     */
    bool PatternTree::totalSupportForRangeHelper(const Constraints & c,
                                                 uint from, uint to,
                                                 uint & totalSupport,
                                                 const ItemIDList & prefix,
                                                 const FPNode<TiltedTimeWindow> * node)
    {
        ItemIDList frequentItemset = prefix;
        frequentItemset << node->getItemID();
        bool matches = false;

        // Recursive call for each child node of the current node.
        foreach (const FPNode<TiltedTimeWindow> * child, node->getChildren()) {
            matches = PatternTree::totalSupportForRangeHelper(c, from, to,
                                                              totalSupport,
                                                              frequentItemset,
                                                              child)
                      || matches;
        }

        // Only check at the leaf level, and try to match again if it didn't
        // match, because it's possible that the superset contains an item that
        // is in a negative item constraint, causing the match to fail.
        if (!matches || node->numChildren() == 0)
            matches = c.matchItemset(frequentItemset);

        if (matches)
            totalSupport += node->getValue().getSupportForRange(from, to);

        return matches;
    }


    //------------------------------------------------------------------------
    // Other.

#ifdef DEBUG
    QDebug operator<<(QDebug dbg, const PatternTree & tree) {
        dbg.nospace() << dumpHelper(*(tree.getRoot())).toStdString().c_str();

        return dbg.nospace();
    }

    QString dumpHelper(const FPNode<TiltedTimeWindow> & node, QString prefix) {
        static QString suffix = "\t";
        QString s;
        bool firstChild = true;

        // Print current node.
        QDebug(&s) << node << "\n";

        // Print all child nodes.
        if (node.numChildren() > 0) {
            foreach (FPNode<TiltedTimeWindow> * child, node.getChildren()) {
                if (firstChild)
                    s += prefix;
                else
                    firstChild = false;
                s += "-> " + dumpHelper(*child, prefix + suffix);
            }
        }

        return s;
    }

    QDebug operator<<(QDebug dbg, const FPNode<TiltedTimeWindow> & node) {
        if (node.getItemID() == ROOT_ITEMID)
            dbg.nospace() << "(NULL)";
        else {
            QString nodeID;

            ItemIDList pattern = PatternTree::getPatternForNode(&node);
            nodeID.sprintf("0x%04d", node.getNodeID());

            dbg.nospace() << "({";
            itemIDHelper(dbg, pattern, node.itemIDNameHash);
            dbg.nospace() << "}, " << node.getValue() << ") (" << nodeID.toStdString().c_str() <<  ")";
        }

        return dbg.nospace();
    }
#endif

}
