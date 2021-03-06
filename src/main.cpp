#include "gl.h"
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>
#include <lodepng.h>
#include <map>
#include <cstdint>

#include "objparser.h"

// some utility code is tucked away in main.h
// for example, drawing the coordinate axes
// or helpers for setting uniforms.
#include "main.h"

#include "iostream"

using namespace std;
// 4096x4096 is a pretty large texture. Extensions to shadow algorithm
// (extra credit) help lowering this memory footprint.
const int SHADOW_WIDTH = 4096;
const int SHADOW_HEIGHT = 4096;

// FUNCTION DECLARATIONS - you will implement these
void loadTextures();
void freeTextures();

void loadFramebuffer();
void freeFramebuffer();

void draw();

Matrix4f getLightView();
Matrix4f getLightProjection();

// Globals here.
objparser scene;
Vector3f  light_dir;
glfwtimer timer;
VertexRecorder rec;
std::map<std::string, GLuint> glTextures;

GLuint fb; // framebuffer handle
GLuint fb_depthtex; // framebuffer depth texture handle
GLuint fb_colortex; // framebuffer color texture handle

// animate light source direction
void updateLightDirection() {
    float elapsed_s = timer.elapsed();
    //elapsed_s = 88.88f;
    float timescale = 0.1f;
    light_dir = Vector3f(2.0f * sinf((float)elapsed_s * 1.5f * timescale),
                         5.0f, 2.0f * cosf(2 + 1.9f * (float)elapsed_s * timescale));
    light_dir.normalize();
}


void drawScene(GLint program, Matrix4f V, Matrix4f P) {
    
    Matrix4f M = Matrix4f::identity();
    updateTransformUniforms( program, M, V, P);
    
    int i = 0;
    for(draw_batch batch : scene.batches) {
        int ii = 0;
        for (int ii = batch.start_index; ii < batch.start_index + batch.nindices; ii++) {
	  int currentBatchIndex = scene.indices[ii];
	  rec.record(
		     scene.positions[currentBatchIndex],
                       scene.normals[currentBatchIndex],
		     Vector3f(scene.texcoords[currentBatchIndex][0], scene.texcoords[currentBatchIndex][1], 0));
	  
        }
        
        updateMaterialUniforms( program, batch.mat.diffuse, batch.mat.ambient, batch.mat.specular, batch.mat.shininess);
        
        // Diffuse Texture handling:
        GLuint texture = glTextures[batch.mat.diffuse_texture];
        glBindTexture(GL_TEXTURE_2D, texture);
        
	// bind the depth texture to texture1; switch back
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, fb_depthtex);
	glActiveTexture(GL_TEXTURE0);
	
	// Shadow mapping:
	int loc = glGetUniformLocation(program, "shadowTex");
	glUniform1i(loc, 1) ; // bind sample to texture unit 
	
	Matrix4f vp =  getLightProjection() * getLightView();
        int matrixloc = glGetUniformLocation(program, "light_VP");
        glUniformMatrix4fv(matrixloc, 1, false, vp);

	rec.draw();
        i++;
        rec.clear();
    }
}

void draw() {
    
    // 1. LIGHT PASS
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int winw, winh;
    glfwGetFramebufferSize(window, &winw, &winh);
    glViewport(0, 0, winw, winh);
    glUseProgram(program_light);
    updateLightUniforms(program_light, light_dir, Vector3f(1.2f, 1.2f, 1.2f));

    drawScene(program_light, camera.GetViewMatrix(), camera.GetPerspective());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // 2. DEPTH PASS
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glUseProgram(program_color);

    drawScene(program_color, getLightView(), getLightProjection());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // 3. DRAW DEPTH TEXTURE AS QUAD
    glViewport(0, 0, 256, 256);
    drawTexturedQuad(fb_depthtex);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(256, 0, 256, 256);
    drawTexturedQuad(fb_colortex);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void loadTextures() {
    for(auto it = scene.textures.begin(); it != scene.textures.end(); ++it) {
        GLuint glTexture;
        std::string name = it->first;
        rgbimage& im = it->second;
        
        glGenTextures(1, &glTexture);
        
        glBindTexture(GL_TEXTURE_2D, glTexture);

        // Allocate storage for texture; upload pixel data
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, im.w, im.h, 0, GL_RGB, GL_UNSIGNED_BYTE, im.data.data());

        // Enable BiLinear Filtering
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        // Store texture
        glTextures.insert(std::make_pair(name, glTexture));
    }
}

void freeTextures() {
    for( auto it = glTextures.begin(); it != glTextures.end(); ++it) {
        std::string name = it->first;
        GLuint glTexture = it->second;
        
        glDeleteTextures(1, &glTexture);
    }
    glTextures.clear();
}

void loadFramebuffer() {
  glGenTextures(1, &fb_depthtex);
  glGenTextures(1, &fb_colortex);
    
  // Handle color texture:
  glBindTexture(GL_TEXTURE_2D, fb_colortex);
  
  // Allocate storage for color texture; will be filled by rendering into the texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4096, 4096, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  
  // configure texture interpolation settings
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  
  // Handle depth texture:
  glBindTexture(GL_TEXTURE_2D, fb_depthtex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 4096, 4096, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  
  // Request handle for framebuffer
  glGenFramebuffers(1, &fb);

  // bind current framebuffer object
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_colortex, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb_depthtex, 0);
  
  // check configuration:
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if( status != GL_FRAMEBUFFER_COMPLETE) {
    printf("Error, incomplete framebuffer\n");
    exit(-1);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void freeFramebuffer() {
   glDeleteTextures(1, &fb_depthtex);
   glDeleteTextures(1, &fb_colortex);
   glDeleteFramebuffers(1, &fb);
}

Matrix4f getLightView() {
  Vector3f center(0,0,0);
  Vector3f up(light_dir.z(), light_dir.z(), -light_dir.x() - light_dir.y());
  up.normalize();
  Vector3f eye( light_dir * 50.0f);

  return Matrix4f::lookAt( eye, center, up);
}

Matrix4f getLightProjection() {
  return Matrix4f::orthographicProjection(64, 64, 8, 100, false);
}

// Main routine.
// Set up OpenGL, define the callbacks and start the main loop
int main(int argc, char* argv[])
{
    std::string basepath = "./";
    if (argc > 2) {
        printf("Usage: %s [basepath]\n", argv[0]);
    }
    else if (argc == 2) {
        basepath = argv[1];
    }
    printf("Loading scene and shaders relative to path %s\n", basepath.c_str());
    
    // load scene data
    // parsing code is in objparser.cpp
    // take a look at the public interface in objparser.h
    if (!scene.parse(basepath + "data/sponza_low/sponza_norm.obj")) {
        return -1;
    }
    
    rec = VertexRecorder();
    
    window = createOpenGLWindow(1024, 1024, "Assignment 5");
    
    // setup the event handlers
    // key handlers are defined in main.h
    // take a look at main.h to know what's in there.
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);
    glfwSetCursorPosCallback(window, motionCallback);
    
    glClearColor(0.8f, 0.8f, 1.0f, 1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    loadTextures();
    loadFramebuffer();
    
    camera.SetDimensions(600, 600);
    camera.SetPerspective(50);
    camera.SetDistance(10);
    camera.SetCenter(Vector3f(0, 1, 0));
    camera.SetRotation(Matrix4f::rotateY(1.6f) * Matrix4f::rotateZ(0.4f));
    
    // set timer for animations
    timer.set();
    while (!glfwWindowShouldClose(window)) {
        setViewportWindow(window);
        
        // we reload the shader files each frame.
        // this shaders can be edited while the program is running
        // loadPrograms/freePrograms is implemented in main.h
        bool valid_shaders = loadPrograms(basepath);
        if (valid_shaders) {
            
            // draw coordinate axes
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (gMousePressed) {
                drawAxis();
            }
            
            // update animation
            updateLightDirection();
            
            // draw everything
            draw();
        }
        // make sure to release the shader programs.
        freePrograms();
        
        // Make back buffer visible
        glfwSwapBuffers(window);
        
        // Check if any input happened during the last frame
        glfwPollEvents();
    } // END OF MAIN LOOP
    
    // All OpenGL resource that are created with
    // glGen* or glCreate* must be freed.
    freeFramebuffer();
    freeTextures();
    
    glfwDestroyWindow(window);
    
    
    return 0;	// This line is never reached.
}
