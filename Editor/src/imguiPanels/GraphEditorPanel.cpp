#include "GraphEditorPanel.h"
#include <imgui.h>

GraphEditorPanel::GraphEditorPanel()
{
    setupDemoGraph();
}

GraphEditorPanel::~GraphEditorPanel() {}

void GraphEditorPanel::setupDemoGraph()
{
    using namespace Modules;

    // Create input nodes
    GraphNode inputA;
    inputA.name = "Color A";
    inputA.opType = NodeOpType::INPUT;
    inputA.windowPosition = ImVec2(0, 0);
    inputA.color = ImVec4(0.2f, 0.3f, 0.5f, 1.0f);

    NodeParameter colorAOut;
    colorAOut.name = "Color";
    colorAOut.pType = ParameterType::VEC3;
    colorAOut.value = glm::vec3(1.0f, 0.0f, 0.0f); // Red
    inputA.outputs.push_back(colorAOut);

    GraphNode inputB;
    inputB.name = "Color B";
    inputB.opType = NodeOpType::INPUT;
    inputB.windowPosition = ImVec2(0, 192);
    inputB.color = ImVec4(0.2f, 0.3f, 0.5f, 1.0f);

    NodeParameter colorBOut;
    colorBOut.name = "Color";
    colorBOut.pType = ParameterType::VEC3;
    colorBOut.value = glm::vec3(0.0f, 0.0f, 1.0f); // Blue
    inputB.outputs.push_back(colorBOut);

    GraphNode alphaInput;
    alphaInput.name = "Mix Factor";
    alphaInput.opType = NodeOpType::INPUT;
    alphaInput.windowPosition = ImVec2(0, 384);
    alphaInput.color = ImVec4(0.2f, 0.3f, 0.5f, 1.0f);

    NodeParameter alphaOut;
    alphaOut.name = "Value";
    alphaOut.pType = ParameterType::F32;
    alphaOut.value = 0.5f;
    alphaInput.outputs.push_back(alphaOut);

    // Create output node
    GraphNode output;
    output.name = "PrimaryMaterialNode";
    output.opType = NodeOpType::OUTPUT;
    output.windowPosition = ImVec2(640, 128);
    output.color = ImVec4(0.5f, 0.2f, 0.2f, 1.0f);

    NodeParameter finalColorIn;
    finalColorIn.name = "Albedo";
    finalColorIn.pType = ParameterType::VEC3;
    output.inputs.push_back(finalColorIn);

    NodeParameter finalMetallicIn;
    finalMetallicIn.name = "Metallic";
    finalMetallicIn.pType = ParameterType::F32;
    output.inputs.push_back(finalMetallicIn);

    NodeParameter finalRougnessIn;
    finalRougnessIn.name = "Rougness";
    finalRougnessIn.pType = ParameterType::F32;
    output.inputs.push_back(finalRougnessIn);

    NodeParameter finalEmissionIn;
    finalEmissionIn.name = "Emission";
    finalEmissionIn.pType = ParameterType::VEC3;
    output.inputs.push_back(finalEmissionIn);

    NodeParameter finalEmissionStrIn;
    finalEmissionStrIn.name = "Emission Strength";
    finalEmissionStrIn.pType = ParameterType::F32;
    output.inputs.push_back(finalEmissionStrIn);

    NodeParameter finalAlbedoMapIn;
    finalAlbedoMapIn.name = "Albedo Map";
    finalAlbedoMapIn.pType = ParameterType::TEXTURE_HANDLE;
    output.inputs.push_back(finalAlbedoMapIn);

    NodeParameter finalNormalMapIn;
    finalNormalMapIn.name = "Normal Map";
    finalNormalMapIn.pType = ParameterType::TEXTURE_HANDLE;
    output.inputs.push_back(finalNormalMapIn);

    NodeParameter finalORMMapIn;
    finalORMMapIn.name = "Occlusion, Rougness, Metallic Map";
    finalORMMapIn.pType = ParameterType::TEXTURE_HANDLE;
    output.inputs.push_back(finalORMMapIn);

    // Create mix node
    GraphNode mixNode;
    mixNode.name = "Mix";
    mixNode.opType = NodeOpType::MIX;
    mixNode.windowPosition = ImVec2(384, 128);
    mixNode.color = ImVec4(0.3f, 0.2f, 0.4f, 1.0f);

    NodeParameter mixInA;
    mixInA.name = "A";
    mixInA.pType = ParameterType::VEC3;
    mixNode.inputs.push_back(mixInA);

    NodeParameter mixInB;
    mixInB.name = "B";
    mixInB.pType = ParameterType::VEC3;
    mixNode.inputs.push_back(mixInB);

    NodeParameter mixInAlpha;
    mixInAlpha.name = "Alpha";
    mixInAlpha.pType = ParameterType::F32;
    mixNode.inputs.push_back(mixInAlpha);

    NodeParameter mixOut;
    mixOut.name = "Result";
    mixOut.pType = ParameterType::VEC3;
    mixNode.outputs.push_back(mixOut);

    // Create graph with inputs and output
    std::vector<GraphNode> inputs = {inputA, inputB, alphaInput};
    std::vector<GraphNode> outputs = {output};

    m_graph = std::make_shared<Graph>(inputs, outputs);

    // Add the mix node
    uint32_t mixId = m_graph->addNode(mixNode);

    // Get the node IDs (they were assigned by the graph)
    auto &nodes = m_graph->getNodes();
    uint32_t inputAId = 0;
    uint32_t inputBId = 0;
    uint32_t alphaId = 0;
    uint32_t outputId = 0;

    // Find the input/output node IDs by their names
    for (auto &[id, node] : nodes) {
        if (node.name == "Color A") inputAId = id;
        else if (node.name == "Color B") inputBId = id;
        else if (node.name == "Mix Factor") alphaId = id;
        else if (node.name == "Final Color") outputId = id;
    }

    // Create connections
    // Input A -> Mix.A
    NodeConnection conn1;
    conn1.fromNode = inputAId;
    conn1.toNode = mixId;
    conn1.outputIndex = 0;
    conn1.inputIndex = 0;
    m_graph->link(conn1);

    // Input B -> Mix.B
    NodeConnection conn2;
    conn2.fromNode = inputBId;
    conn2.toNode = mixId;
    conn2.outputIndex = 0;
    conn2.inputIndex = 1;
    m_graph->link(conn2);

    // Alpha Input -> Mix.Alpha
    NodeConnection conn3;
    conn3.fromNode = alphaId;
    conn3.toNode = mixId;
    conn3.outputIndex = 0;
    conn3.inputIndex = 2;
    m_graph->link(conn3);

    // Mix -> Output
    NodeConnection conn4;
    conn4.fromNode = mixId;
    conn4.toNode = outputId;
    conn4.outputIndex = 0;
    conn4.inputIndex = 0;
    m_graph->link(conn4);

    // Create the editor
    m_editor = std::make_unique<Modules::GraphEditor>("Material Graph", m_graph, ImVec2(1200, 800));
}

void GraphEditorPanel::render()
{
    ImGui::Begin("Graph Editor Demo");

    if (m_editor) {
        m_editor->render();
    }

    ImGui::End();
}
