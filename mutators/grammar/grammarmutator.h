#pragma once

#include "../../mutator.h"
#include "../../mutex.h"
#include "grammar.h"

// The mutator context for Grammar mutator
// is the tree structure of the current sample
// 语法变异器的样本变异上下文环境
class GrammarMutatorContext : public MutatorSampleContext
{
public:
    GrammarMutatorContext(Sample *sample, Grammar *grammar);

    Grammar::TreeNode *tree;
};

// 语法变异器，其中包含多种变异方式，比如Replace，Splice，Repeat等，语法fuzz中使用
class GrammarMutator : public Mutator
{
public:
    // Mutator interface method
    GrammarMutator(Grammar *grammar) :
        grammar(grammar)
    {
    }
    bool CanGenerateSample() override
    {
        return true;
    }
    bool GenerateSample(Sample *sample, PRNG *prng) override;
    void InitRound(Sample *input_sample, MutatorSampleContext *context) override;
    // 语法变异器变异函数
    bool Mutate(Sample *inout_sample, PRNG *prng, std::vector<Sample *> &all_samples) override;
    // 创建样本变异上下文环境，作为当前样本的树结构
    MutatorSampleContext *CreateSampleContext(Sample *sample) override;

protected:
    // MUTATORS:

    // 1) Re-generates a random node
    //对当前树的一个节点的具体展开方式使用语法文件中的一种展开方式进行替换，即针对children利用语法文件进行变异
    int ReplaceNode(Grammar::TreeNode *tree, PRNG *prng);

    // 2) Replaces a node from the current sample
    //    With an equivalent node from another sample
    //对当前样本树的节点使用其他样本树的同名节点进行替换，即针对Node利用其他样本节点树的节点进行变异
    int Splice(Grammar::TreeNode *tree, PRNG *prng);

    // 3) Selects a <repeat> node from the current sample
    //    and adds/potentially removes children from it
    //对当前树的一个重复节点的具体展开方式使用语法文件中的一种重复节点的展开方式进行重复的递增或是删减，即针对children利用语法文件进行变异
    int RepeatMutator(Grammar::TreeNode *tree, PRNG *prng);

    // 4) Selects a <repeat> node from the current sample
    //    and a similar <repeat> node from another sample.
    //    Mixes children from the other node into the current node.
    //对当前树的一个重复节点的具体展开方式使用其他样本树的同名重复节点的展开方式进行重复的递增或是删减，即针对Node利用其他样本节点树的节点进行变异
    int RepeatSplice(Grammar::TreeNode *tree, PRNG *prng);

    int InsertMutator(Grammar::TreeNode *tree, PRNG *prng);
    int InsertSplice(Grammar::TreeNode *tree, PRNG *prng);

    // repeately attempts to generate a tree until an attempt is successful
    Grammar::TreeNode *GenerateTreeNoFail(Grammar::Symbol *symbol, PRNG *prng);
    Grammar::TreeNode *GenerateTreeNoFail(const char *symbol, PRNG *prng);

    Grammar::TreeNode *current_sample;

    struct MutationCandidate
    {
        Grammar::TreeNode *node;
        int depth;
        double p;
    };

    // list of candidatate tree nodes for mutation
    // allocated here to avoid allocaing for each iteration
    //四类待变异的候选变异节点容器，在变异一开始将样本的节点加入到变异容器中
    //正常变异候选节点容器
    std::vector<MutationCandidate> candidates;
    //重复变异候选节点容器
    std::vector<MutationCandidate> repeat_candidates;
    //在进行Splice变异时，从感兴趣的节点树中挑选树的节点加入到Splice变异容器中
    std::vector<MutationCandidate> splice_candidates;
    //在进行Insert变异时，从感兴趣的节点树中挑选树的节点加入到Insert变异容器中
    std::vector<MutationCandidate> insert_candidates;

    // creates a list of mutation candidates based on params
    //从给定的节点树选取符合条件的节点加入候选变异节点容器
    double GetMutationCandidates(std::vector<MutationCandidate> &candidates, Grammar::TreeNode *node, Grammar::Symbol *filter, int depth, int maxdepth, double p, bool just_repeat = false);

    // selects a node to mutate from a list of candidates based on candidate probability
    //从候选变异节点容器中随机挑选一个变异节点
    MutationCandidate *GetNodeToMutate(std::vector<MutationCandidate> &candidates, PRNG *prng);

    // global vector of other trees with unique coverage
    // used by splice mutator etc.
    static std::vector<Grammar::TreeNode *> interesting_trees;
    Mutex interesting_trees_mutex;

    // 语法fuzz时输入的语法文件
    Grammar *grammar;
};
