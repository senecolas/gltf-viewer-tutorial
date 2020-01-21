#include "ViewerApplication.hpp"

#include <iostream>
#include <numeric>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/io.hpp>

#include "utils/cameras.hpp"

#include <stb_image_write.h>
#include <tiny_gltf.h>

void keyCallback(
    GLFWwindow *window, int key, int scancode, int action, int mods)
{
  if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE) {
    glfwSetWindowShouldClose(window, 1);
  }
}

int ViewerApplication::run()
{
  // Loader shaders
  const auto glslProgram =
      compileProgram({m_ShadersRootPath / m_AppName / m_vertexShader,
          m_ShadersRootPath / m_AppName / m_fragmentShader});

  const auto modelViewProjMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uModelViewProjMatrix");
  const auto modelViewMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uModelViewMatrix");
  const auto normalMatrixLocation =
      glGetUniformLocation(glslProgram.glId(), "uNormalMatrix");

  // Build projection matrix
  auto maxDistance = 500.f; // TODO use scene bounds instead to compute this
  maxDistance = maxDistance > 0.f ? maxDistance : 100.f;
  const auto projMatrix =
      glm::perspective(70.f, float(m_nWindowWidth) / m_nWindowHeight,
          0.001f * maxDistance, 1.5f * maxDistance);

  // TODO Implement a new CameraController model and use it instead. Propose the
  // choice from the GUI
  FirstPersonCameraController cameraController{
      m_GLFWHandle.window(), 0.5f * maxDistance};
  if (m_hasUserCamera) {
    cameraController.setCamera(m_userCamera);
  } else {
    // TODO Use scene bounds to compute a better default camera
    cameraController.setCamera(
        Camera{glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)});
  }

  tinygltf::Model model;

  // ++ Loading the glTF file
  if (!loadGltfFile(model)) {
    return -1;
  }

  // ++ Creation of Buffer Objects
  const auto bufferObjects = createBufferObjects(model);

  // ++ Creation of Vertex Array Objects
  std::vector<VaoRange> meshToVertexArrays;
  const auto vertexArrayObjects = createVertexArrayObjects(model, bufferObjects, meshToVertexArrays);

  // Setup OpenGL state for rendering
  glEnable(GL_DEPTH_TEST);
  glslProgram.use();

  // Lambda function to draw the scene
  const auto drawScene = [&](const Camera &camera) {
    glViewport(0, 0, m_nWindowWidth, m_nWindowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto viewMatrix = camera.getViewMatrix();

    // The recursive function that should draw a node
    // We use a std::function because a simple lambda cannot be recursive
    const std::function<void(int, const glm::mat4 &)> drawNode =
        [&](int nodeIdx, const glm::mat4 &parentMatrix) {
          // ++ The drawNode function
          const auto &node = model.nodes[nodeIdx];
          const glm::mat4 modelMatrix = getLocalToWorldMatrix(node, parentMatrix);

          // if the node has a mesh
          if (node.mesh >= 0) {
            const auto mvMatrix = viewMatrix * modelMatrix; // Also called localToCamera matrix
            const auto mvpMatrix = projMatrix * mvMatrix; // Also called localToScreen matrix
            const auto normalMatrix = glm::transpose(glm::inverse(mvMatrix));

            // send to the shaders
            glUniformMatrix4fv(modelViewProjMatrixLocation, 1, GL_FALSE, glm::value_ptr(mvpMatrix));
            glUniformMatrix4fv(modelViewMatrixLocation, 1, GL_FALSE, glm::value_ptr(mvMatrix));
            glUniformMatrix4fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));

            // draw primitives
            const auto &mesh = model.meshes[node.mesh];
            const auto &vaoRange = meshToVertexArrays[node.mesh];
            for (size_t pIdx = 0; pIdx < mesh.primitives.size(); ++pIdx) {
              const auto vao = vertexArrayObjects[vaoRange.begin + pIdx];
              const auto &primitive = mesh.primitives[pIdx];
              glBindVertexArray(vao);
              if (primitive.indices >= 0){
                const auto &accessor = model.accessors[primitive.indices];
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;
                glDrawElements(primitive.mode, GLsizei(accessor.count), accessor.componentType, (const GLvoid *)byteOffset);
              } else {
                // Take first accessor to get the count
                const auto accessorIdx = (*begin(primitive.attributes)).second;
                const auto &accessor = model.accessors[accessorIdx];
                glDrawArrays(primitive.mode, 0, GLsizei(accessor.count));
              }
            }
          }
          
          // Draw children
          for (const auto childNodeIdx : node.children) {
            drawNode(childNodeIdx, modelMatrix);
          }

        };

    // Draw the scene referenced by gltf file
    if (model.defaultScene >= 0) {
      // ++ Draw all nodes
      for(const auto nodeIdx : model.scenes[model.defaultScene].nodes){
        drawNode(nodeIdx, glm::mat4(1));
      }
    }
    glBindVertexArray(0);
  };

  // Loop until the user closes the window
  for (auto iterationCount = 0u; !m_GLFWHandle.shouldClose();
       ++iterationCount) {
    const auto seconds = glfwGetTime();

    const auto camera = cameraController.getCamera();
    drawScene(camera);

    // GUI code:
    imguiNewFrame();

    {
      ImGui::Begin("GUI");
      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
          1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("eye: %.3f %.3f %.3f", camera.eye().x, camera.eye().y,
            camera.eye().z);
        ImGui::Text("center: %.3f %.3f %.3f", camera.center().x,
            camera.center().y, camera.center().z);
        ImGui::Text(
            "up: %.3f %.3f %.3f", camera.up().x, camera.up().y, camera.up().z);

        ImGui::Text("front: %.3f %.3f %.3f", camera.front().x, camera.front().y,
            camera.front().z);
        ImGui::Text("left: %.3f %.3f %.3f", camera.left().x, camera.left().y,
            camera.left().z);

        if (ImGui::Button("CLI camera args to clipboard")) {
          std::stringstream ss;
          ss << "--lookat " << camera.eye().x << "," << camera.eye().y << ","
             << camera.eye().z << "," << camera.center().x << ","
             << camera.center().y << "," << camera.center().z << ","
             << camera.up().x << "," << camera.up().y << "," << camera.up().z;
          const auto str = ss.str();
          glfwSetClipboardString(m_GLFWHandle.window(), str.c_str());
        }
      }
      ImGui::End();
    }

    imguiRenderFrame();

    glfwPollEvents(); // Poll for and process events

    auto ellapsedTime = glfwGetTime() - seconds;
    auto guiHasFocus =
        ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
    if (!guiHasFocus) {
      cameraController.update(float(ellapsedTime));
    }

    m_GLFWHandle.swapBuffers(); // Swap front and back buffers
  }

  // TODO clean up allocated GL data

  return 0;
}

bool ViewerApplication::loadGltfFile(tinygltf::Model &model){

  std::clog << "Loading file " << m_gltfFilePath << std::endl;

  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, m_gltfFilePath.string());

  if (!warn.empty()) {
    printf("Warn: %s\n", warn.c_str());
  }

  if (!err.empty()) {
    printf("Err: %s\n", err.c_str());
  }

  if (!ret) {
    printf("Failed to parse glTF\n");
  }

  return ret;
}

std::vector<GLuint> ViewerApplication::createBufferObjects(const tinygltf::Model &model){
  std::vector<GLuint> bufferObjects(model.buffers.size(), 0);
  glGenBuffers(bufferObjects.size(), bufferObjects.data()); // Ask opengl to reserve an identifier for our buffer object and store it in bufferObject.
  for (size_t bufferIdx = 0; bufferIdx < model.buffers.size(); ++bufferIdx) {
    glBindBuffer(GL_ARRAY_BUFFER, bufferObjects[bufferIdx]);
    glBufferStorage(GL_ARRAY_BUFFER, model.buffers[bufferIdx].data.size(), 
        model.buffers[bufferIdx].data.data(), 0);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0); // Cleanup the binding point after the loop only
  return bufferObjects;
}

std::vector<GLuint> ViewerApplication::createVertexArrayObjects(
      const tinygltf::Model &model, 
      const std::vector<GLuint> &bufferObjects, 
      std::vector<VaoRange> & meshIndexToVaoRange){

  std::vector<GLuint> vertexArrayObjects;

  std::map<std::string, GLuint> vertex_attributes;
  vertex_attributes.insert(std::make_pair("POSITION", 0));
  vertex_attributes.insert(std::make_pair("NORMAL", 1));
  vertex_attributes.insert(std::make_pair("TEXCOORD_0", 2));

  for(const auto &mesh : model.meshes){
    const auto vaoOffset = vertexArrayObjects.size();
    vertexArrayObjects.resize(vaoOffset + mesh.primitives.size());
    meshIndexToVaoRange.push_back(VaoRange{GLsizei(vaoOffset), GLsizei(mesh.primitives.size())}); // Will be used during rendering

    glGenVertexArrays(mesh.primitives.size(), &vertexArrayObjects[vaoOffset]);

    for (size_t pIdx = 0; pIdx < mesh.primitives.size(); ++pIdx){
      const auto vao = vertexArrayObjects[vaoOffset + pIdx];
      const auto &primitive = mesh.primitives[pIdx];
      glBindVertexArray(vao);

      for (const auto vertex_attribute : vertex_attributes){ // TO DO, LOOP for normal etc...
        const auto iterator = primitive.attributes.find(vertex_attribute.first);
        if (iterator == end(primitive.attributes)) // If the attribut has not been found in the map
          continue;
        
        // (*iterator).first is the key "POSITION", (*iterator).second is the value, ie. the index of the accessor for this attribute
        const auto accessorIdx = (*iterator).second;
        const auto &accessor = model.accessors[accessorIdx]; // ++ get the correct tinygltf::Accessor from model.accessors
        const auto &bufferView = model.bufferViews[accessor.bufferView]; // ++ get the correct tinygltf::BufferView from model.bufferViews. You need to use the accessor
        const auto bufferIdx = bufferView.buffer; // ++ get the index of the buffer used by the bufferView (you need to use it)

        glEnableVertexAttribArray(vertex_attribute.second); // ++ Enable the vertex attrib array corresponding to POSITION with glEnableVertexAttribArray (you need to use VERTEX_ATTRIB_POSITION_IDX which is defined at the top of the file)
        assert(GL_ARRAY_BUFFER == bufferView.target); // steal from cheat
        glBindBuffer(GL_ARRAY_BUFFER, bufferObjects[bufferIdx]); // ++ Bind the buffer object to GL_ARRAY_BUFFER

        const auto byteOffset = bufferView.byteOffset + accessor.byteOffset; // ++ Compute the total byte offset using the accessor and the buffer view

        // ++ Call glVertexAttribPointer with the correct arguments. 
        // Remember size is obtained with accessor.type, type is obtained with accessor.componentType. 
        // The stride is obtained in the bufferView, normalized is always GL_FALSE, and pointer is the byteOffset (don't forget the cast).
        glVertexAttribPointer(vertex_attribute.second, 
                              accessor.type, 
                              accessor.componentType,
                              GL_FALSE,
                              GLsizei(bufferView.byteStride),
                              (const GLvoid*) byteOffset);
      }

      // Index array if defined
      if (primitive.indices >= 0) {
        const auto accessorIdx = primitive.indices;
        const auto &accessor = model.accessors[accessorIdx];
        const auto &bufferView = model.bufferViews[accessor.bufferView];
        const auto bufferIdx = bufferView.buffer;

        assert(GL_ELEMENT_ARRAY_BUFFER == bufferView.target);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferObjects[bufferIdx]);  
      }
    }
  }
  glBindVertexArray(0);
  std::clog << "Number of VAOs: " << vertexArrayObjects.size() << std::endl;
  return vertexArrayObjects;
}

ViewerApplication::ViewerApplication(const fs::path &appPath, uint32_t width,
    uint32_t height, const fs::path &gltfFile,
    const std::vector<float> &lookatArgs, const std::string &vertexShader,
    const std::string &fragmentShader, const fs::path &output) :
    m_nWindowWidth(width),
    m_nWindowHeight(height),
    m_AppPath{appPath},
    m_AppName{m_AppPath.stem().string()},
    m_ImGuiIniFilename{m_AppName + ".imgui.ini"},
    m_ShadersRootPath{m_AppPath.parent_path() / "shaders"},
    m_gltfFilePath{gltfFile},
    m_OutputPath{output}
{
  if (!lookatArgs.empty()) {
    m_hasUserCamera = true;
    m_userCamera =
        Camera{glm::vec3(lookatArgs[0], lookatArgs[1], lookatArgs[2]),
            glm::vec3(lookatArgs[3], lookatArgs[4], lookatArgs[5]),
            glm::vec3(lookatArgs[6], lookatArgs[7], lookatArgs[8])};
  }

  if (!vertexShader.empty()) {
    m_vertexShader = vertexShader;
  }

  if (!fragmentShader.empty()) {
    m_fragmentShader = fragmentShader;
  }

  ImGui::GetIO().IniFilename =
      m_ImGuiIniFilename.c_str(); // At exit, ImGUI will store its windows
                                  // positions in this file

  glfwSetKeyCallback(m_GLFWHandle.window(), keyCallback);

  printGLVersion();
}