#ifndef RAPTURE__GRAPH_NODE_EVALUATORS_H
#define RAPTURE__GRAPH_NODE_EVALUATORS_H

#include "Graph.h"
#include <functional>
#include <unordered_map>

namespace Modules {

using NodeEvaluator = std::function<bool(GraphNode &)>;

// Initialize the evaluator map with all node operation types
void initializeEvaluators(std::unordered_map<NodeOpType, NodeEvaluator> &evaluators);

// Individual evaluator functions
bool s_evaluateInput(GraphNode &node);
bool s_evaluateOutput(GraphNode &node);
bool s_evaluateAdd(GraphNode &node);
bool s_evaluateSubtract(GraphNode &node);
bool s_evaluateMultiply(GraphNode &node);
bool s_evaluateDivide(GraphNode &node);
bool s_evaluateMix(GraphNode &node);
bool s_evaluateClamp(GraphNode &node);
bool s_evaluateLength(GraphNode &node);
bool s_evaluateNormalize(GraphNode &node);
bool s_evaluateSplit(GraphNode &node);
bool s_evaluateGroup(GraphNode &node);

} // namespace Modules

#endif // RAPTURE__GRAPH_NODE_EVALUATORS_H
