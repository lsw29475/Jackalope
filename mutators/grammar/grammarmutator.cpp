#include "common.h"
#include "grammarmutator.h"

#define MUTATOR_REPEAT_PROB 0.5

std::vector<Grammar::TreeNode *> GrammarMutator::interesting_trees;

Grammar::TreeNode *GrammarMutator::GenerateTreeNoFail(Grammar::Symbol *symbol, PRNG *prng)
{
    Grammar::TreeNode *tree = NULL;
    size_t i = 0;
    do {
        if ((i > 0) && (i % 100 == 0))
        {
            WARN("Repeatedly failing to generate sample from grammar\n");
        }
        tree = grammar->GenerateTree(symbol, prng);
    } while (!tree);
    return tree;
}

Grammar::TreeNode *GrammarMutator::GenerateTreeNoFail(const char *symbol, PRNG *prng)
{
    std::string symbol_string;
    symbol_string = symbol;
    Grammar::Symbol *s = grammar->GetSymbol(symbol_string);
    if (!s)
    {
        FATAL("Symbol %s not found in grammar", symbol);
    }
    return GenerateTreeNoFail(s, prng);
}

GrammarMutatorContext::GrammarMutatorContext(Sample *sample, Grammar *grammar)
{
    // 创建样本变异的树结构，先对输入的样本进行解码分析，将样本解码为树节点
    tree = grammar->DecodeSample(sample);
    if (!tree)
    {
        FATAL("Error decoding grammar sample");
    }
}

bool GrammarMutator::GenerateSample(Sample *sample, PRNG *prng)
{
    Grammar::TreeNode *tree = GenerateTreeNoFail("root", prng);
    grammar->EncodeSample(tree, sample);
    delete tree;
    return true;
}

double GrammarMutator::GetMutationCandidates(std::vector<MutationCandidate> &candidates, Grammar::TreeNode *node, Grammar::Symbol *filter, int depth, int maxdepth, double p, bool just_repeat)
{
    if (depth == 0)
    {
        candidates.clear();
    }

    if (depth > maxdepth)
    {
        return 0;
    }

    if (node->type == Grammar::STRINGTYPE)
    {
        return 0;
    }

    if (!filter || (node->symbol == filter))
    {
        //未指定针对固定节点或节点等于指定节点
        if (!just_repeat || node->symbol->repeat)
        {
            //未指定针对重复节点或当前节点为重复节点，将当前节点加入到待变异的节点容器中
            candidates.push_back({node, depth, p});
        }
    }

    for (auto iter = node->children.begin(); iter != node->children.end(); iter++)
    {
        //针对该节点的子节点进行遍历，将符合条件的节点加入到待变异的节点容器
        if ((*iter)->type == Grammar::STRINGTYPE)
        {
            continue;
        }
        GetMutationCandidates(candidates, *iter, filter, depth + 1, maxdepth, p / 1.4, just_repeat);
    }
    return p;
}

GrammarMutator::MutationCandidate *GrammarMutator::GetNodeToMutate(std::vector<MutationCandidate> &candidates, PRNG *prng)
{
    if (candidates.empty())
    {
        return NULL;
    }

    double psum = 0;
    for (int i = 0; i < candidates.size(); i++)
    {
        psum += candidates[i].p;
    }

    if (psum == 0)
    {
        return NULL;
    }

    double p = prng->RandReal() * psum;
    double sum = 0;
    for (int i = 0; i < candidates.size(); i++)
    {
        sum += candidates[i].p;
        if ((p < sum) || (i == (candidates.size() - 1)))
        {
            return &candidates[i];
            break;
        }
    }
    return NULL;
}

MutatorSampleContext *GrammarMutator::CreateSampleContext(Sample *sample)
{
    // 创建样本变异上下文环境，作为当前样本的树结构
    GrammarMutatorContext *context = new GrammarMutatorContext(sample, grammar);
    // we are abusing the fact that CreateSampleContext is only called
    // for interesting samples

    //将当前变异上下文环境中的节点树添加到感兴趣的节点树容器中
    interesting_trees_mutex.Lock();
    interesting_trees.push_back(context->tree);
    interesting_trees_mutex.Unlock();
    return context;
}

void GrammarMutator::InitRound(Sample *input_sample, MutatorSampleContext *context)
{
    current_sample = ((GrammarMutatorContext *)context)->tree;
}

bool GrammarMutator::Mutate(Sample *inout_sample, PRNG *prng, std::vector<Sample *> &all_samples)
{
    Grammar::TreeNode new_sample;

    int mutator_success = 0;
    // in a small number of cases mutator will attempt to generate samples
    double rand_mutator_select = prng->RandReal();
    if (rand_mutator_select < 0.1)
    {
        // 小概率事件可能根据输入的语法文件来生成树节点，之后根据输入的语法文件的节点生成fuzz样本
        Grammar::TreeNode *generated = grammar->GenerateTree("root", prng);
        if (generated)
        {
            new_sample.Replace(generated);
            mutator_success = 1;
        }
    }

    if (mutator_success)
    {
        // 根据输入的语法文件的节点生成fuzz样本
        grammar->EncodeSample(&new_sample, inout_sample);
        return true;
    }

    //大概率事件可能基于当前输入fuzz样本进行变异
    new_sample = *current_sample;
    // otherwise, randomly selects a specific mutator
    // and potentially repeats the process with the same sample
    for (int i = 0; i < 100; i++)
    {
        mutator_success = 0;

        //将当前准备变异的样本节点树的节点添加到待变异的节点容器中
        GetMutationCandidates(candidates, &new_sample, NULL, 0, MAX_DEPTH, 1);
        GetMutationCandidates(repeat_candidates, &new_sample, NULL, 0, MAX_DEPTH, 1, true);

        //根据随机概率，选择对应的变异策略
        double rand_mutator_select = prng->RandReal();
        if (rand_mutator_select < 0.3)
        {
            if (ReplaceNode(&new_sample, prng))
            {
                mutator_success++;
            }
        }
        else if (rand_mutator_select < 0.5)
        {
            if (Splice(&new_sample, prng))
            {
                mutator_success++;
            }
        }
        else if (rand_mutator_select < 0.8)
        {
            if (RepeatMutator(&new_sample, prng))
            {
                mutator_success++;
            }
        }
        else
        {
            if (RepeatSplice(&new_sample, prng))
            {
                mutator_success++;
            }
        }

        if (mutator_success)
        {
            // flip a coin and potentially do another round of mutation
            if (prng->RandReal() > MUTATOR_REPEAT_PROB) break;
        }
    }

    if (!mutator_success)
    {
        WARN("Repeatedly failing to mutate a sample. Check grammar.");
    }

    grammar->EncodeSample(&new_sample, inout_sample);

    return true;
}

int GrammarMutator::ReplaceNode(Grammar::TreeNode *tree, PRNG *prng)
{
    //随机获取一个候选变异节点
    MutationCandidate *mutation_candidate = GetNodeToMutate(candidates, prng);
    if (!mutation_candidate)
    {
        FATAL("Error selecting grammar node to mutate");
    }
    //获取当前候选变异节点的节点树
    Grammar::TreeNode *node_to_mutate = mutation_candidate->node;

    // printf("Mutating node %s\n", node_to_mutate->symbol->name.c_str());
    //从语法文件中随机获取当前候选变异节点的一种展开方式
    Grammar::TreeNode *replacement = grammar->GenerateTree(node_to_mutate->symbol, prng, mutation_candidate->depth);
    if (replacement)
    {
        //将新获取的节点展开方式对旧的展开方式进行替换
        node_to_mutate->Replace(replacement);
        return 1;
    }
    else
    {
        return 0;
    }
}

int GrammarMutator::Splice(Grammar::TreeNode *tree, PRNG *prng)
{
    //随机获取一个候选变异节点
    MutationCandidate *current_candidate = GetNodeToMutate(candidates, prng);
    if (!current_candidate)
    {
        return 0;
    }

    //获取当前候选变异节点的节点树
    Grammar::TreeNode *node = current_candidate->node;

    Grammar::TreeNode *other_tree = NULL;
    size_t num_others = 0;

    //从感兴趣的树容器中随机选取一棵节点树
    interesting_trees_mutex.Lock();
    num_others = interesting_trees.size();
    if (!num_others)
    {
        interesting_trees_mutex.Unlock();
        return 0;
    }
    other_tree = interesting_trees[prng->Rand() % num_others];
    interesting_trees_mutex.Unlock();

    //从感兴趣的节点树中选取符合条件的节点加入Splice待变异节点容器
    GetMutationCandidates(splice_candidates, other_tree, node->symbol, 0, current_candidate->depth, 1);
    if (splice_candidates.empty())
    {
        return 0;
    }

    //获取Splice待变异节点的节点树
    MutationCandidate *other_candidate = GetNodeToMutate(splice_candidates, prng);
    if (!other_candidate)
    {
        return 0;
    }

    //使用新获取的节点对待变异的节点进行替换
    *node = *other_candidate->node;

    return 1;
}

int GrammarMutator::RepeatMutator(Grammar::TreeNode *tree, PRNG *prng)
{
    //从候选的重复节点容器中选取一个重复节点
    if (repeat_candidates.empty())
    {
        return 0;
    }
    MutationCandidate *candidate = GetNodeToMutate(repeat_candidates, prng);
    if (!candidate)
    {
        return 0;
    }
    Grammar::TreeNode *node = candidate->node;

    //随机选取一个重复位置
    int mutate_position = 0;
    if (!node->children.empty())
    {
        mutate_position = prng->Rand() % node->children.size();
    }
    auto iter = node->children.begin();
    int position = 0;
    for (; position < mutate_position; position++)
    {
        iter++;
    }

    //随机选择重复节点删除或添加
    int do_delete = 0;
    int do_insert = 0;
    double rand_mutator_select = prng->RandReal();
    if (rand_mutator_select < 0.2)
    {
        do_delete = 1;
    }
    else if (rand_mutator_select < 0.4)
    {
        do_delete = 1;
        do_insert = 1;
    }
    else
    {
        do_insert = 1;
    }

    int ret = 0;

    // do generation here so we can return early if it failed
    //如果选定添加重复节点，则从语法文件中随机获取一种重复节点的展开式，并随机重复多次
    std::vector<Grammar::TreeNode *> new_children;
    if (do_insert)
    {
        while (1)
        {
            Grammar::TreeNode *child = grammar->GenerateTree(node->symbol->repeat_symbol, prng, candidate->depth + 1);
            if (child)
            {
                new_children.push_back(child);
            }
            if (prng->RandReal() > REPEAT_PROBABILITY)
            {
                break;
            }
        }
        if (new_children.empty())
        {
            return 0;
        }
    }

    //随即删除选定样本的变异节点的部分展开
    if (do_delete)
    {
        while (1)
        {
            if (iter == node->children.end())
            {
                break;
            }
            delete *iter;
            iter = node->children.erase(iter);
            if (prng->RandReal() > REPEAT_PROBABILITY)
            {
                break;
            }
        }
    }

    //随机针对选定样本的变异节点的展开进行插入
    if (do_insert)
    {
        if (iter != node->children.end())
        {
            iter++;
        }

        for (auto iter2 = new_children.begin(); iter2 != new_children.end(); iter2++)
        {
            Grammar::TreeNode *child = *iter2;
            iter = node->children.insert(iter, child);
            iter++;
        }
    }

    return 1;
}

int GrammarMutator::RepeatSplice(Grammar::TreeNode *tree, PRNG *prng)
{
    //从候选的重复节点容器中选取一个重复节点
    if (repeat_candidates.empty())
    {
        return 0;
    }
    MutationCandidate *candidate = GetNodeToMutate(repeat_candidates, prng);
    if (!candidate)
    {
        return 0;
    }
    Grammar::TreeNode *node = candidate->node;

    // TODO: put this in a function
    Grammar::TreeNode *other_tree = NULL;
    size_t num_others = 0;

    interesting_trees_mutex.Lock();
    num_others = interesting_trees.size();
    if (!num_others)
    {
        interesting_trees_mutex.Unlock();
        return 0;
    }
    other_tree = interesting_trees[prng->Rand() % num_others];
    interesting_trees_mutex.Unlock();

    GetMutationCandidates(splice_candidates, other_tree, node->symbol, 0, candidate->depth, 1, true);
    if (splice_candidates.empty())
    {
        return 0;
    }

    MutationCandidate *other_candidate = GetNodeToMutate(splice_candidates, prng);
    if (!other_candidate)
    {
        return 0;
    }

    Grammar::TreeNode *other_node = other_candidate->node;

    int mutate_position = 0;
    if (!node->children.empty())
    {
        mutate_position = prng->Rand() % node->children.size();
    }

    int other_position = 0;
    if (!other_node->children.empty())
    {
        other_position = prng->Rand() % other_node->children.size();
    }

    auto iter = node->children.begin();
    for (int i = 0; i < mutate_position; i++)
    {
        iter++;
    }

    auto other_iter = other_node->children.begin();
    for (int i = 0; i < other_position; i++)
    {
        other_iter++;
    }

    int do_delete = 0;
    double rand_mutator_select = prng->RandReal();
    if (rand_mutator_select < 0.4)
    {
        do_delete = 1;
    }

    if (do_delete)
    {
        while (1)
        {
            if (iter == node->children.end())
            {
                break;
            }
            delete *iter;
            iter = node->children.erase(iter);
            if (prng->RandReal() > REPEAT_PROBABILITY)
            {
                break;
            }
        }
    }

    if (iter != node->children.end())
    {
        iter++;
    }
    while (1)
    {
        if (other_iter == other_node->children.end())
        {
            break;
        }
        Grammar::TreeNode *child = new Grammar::TreeNode(**other_iter);
        iter = node->children.insert(iter, child);
        iter++;
        other_iter++;
        if (prng->RandReal() > REPEAT_PROBABILITY)
        {
            break;
        }
    }

    return 1;
}
