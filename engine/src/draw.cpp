#include <cstdio>
#ifdef __APPLE__
#include <GL/freeglut.h>
#include <GLUT/glut.h>
#elif _WIN32
#include <GL/glew.h>
// DONT REMOVE THIS COMMENT
#include <GL/freeglut.h>
#else
#include <GL/glew.h>
// DONT REMOVE THIS COMMENT
#include <GL/freeglut.h>
#include <GL/glut.h>
#include <cmath>
#endif

#include "catmull_rom.hpp"
#include "draw.hpp"

extern float globalTimer;
extern float timeFactor;
extern bool drawCatmullRomCurves;
extern GLfloat g_viewMatrix[16];

extern WorldConfig config;

// This function assumes an orthonormal view matrix.
void invertViewMatrix(const GLfloat view[16], GLfloat inv[16])
{
    // The upper-left 3x3 is the rotation which is orthonormal.
    inv[0] = view[0];
    inv[1] = view[4];
    inv[2] = view[8];
    inv[3] = 0.0f;
    inv[4] = view[1];
    inv[5] = view[5];
    inv[6] = view[9];
    inv[7] = 0.0f;
    inv[8] = view[2];
    inv[9] = view[6];
    inv[10] = view[10];
    inv[11] = 0.0f;

    // For the translation: inv.translation = -R^T * t
    inv[12] = -(view[12] * view[0] + view[13] * view[1] + view[14] * view[2]);
    inv[13] = -(view[12] * view[4] + view[13] * view[5] + view[14] * view[6]);
    inv[14] = -(view[12] * view[8] + view[13] * view[9] + view[14] * view[10]);
    inv[15] = 1.0f;
}

void updateGroupPosition(GroupConfig& group)
{
    // Allocate an array to hold the 4x4 matrix (OpenGL stores matrices in column-major order)
    GLfloat modelMatrix[16];
    // Retrieves the current modelview matrix (which is V * M_model)
    glGetFloatv(GL_MODELVIEW_MATRIX, modelMatrix);

    // Extract the translation from the model transformation in eye space.
    float tx = modelMatrix[12];
    float ty = modelMatrix[13];
    float tz = modelMatrix[14];

    // Compute the inverse of the view matrix (which we stored globally)
    GLfloat invView[16];
    invertViewMatrix(g_viewMatrix, invView);

    // Multiply the translation (as a 4D vector with w=1) by invView to get world coordinates:
    float worldX = invView[0] * tx + invView[4] * ty + invView[8] * tz + invView[12];
    float worldY = invView[1] * tx + invView[5] * ty + invView[9] * tz + invView[13];
    float worldZ = invView[2] * tx + invView[6] * ty + invView[10] * tz + invView[14];

    group.center.x = worldX;
    group.center.y = worldY;
    group.center.z = worldZ;
}

void drawAxis()
{
    glBegin(GL_LINES);
    // X Axis in red
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(-100.0f, 0.0f, 0.0f);
    glVertex3f(100.0f, 0.0f, 0.0f);
    // Y Axis in Green
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(0.0f, -100.0f, 0.0f);
    glVertex3f(0.0f, 100.0f, 0.0f);
    // Z Axis in Blue
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f(0.0f, 0.0f, -100.0f);
    glVertex3f(0.0f, 0.0f, 100.0f);
    glEnd();
}

void applyTransformations(std::vector<Transform>& transforms)
{
    for (auto& t : transforms) {
        switch (t.type) {
        case TransformType::Translate:
            if (t.curveTime > 0.0f) {
                float pos[3], deriv[3];

                if (drawCatmullRomCurves) {
                    if (config.scene.lighting)
                        glDisable(GL_LIGHTING);
                    glColor3f(1.0f, 1.0f, 0.0f);
                    glBegin(GL_LINE_LOOP);
                    for (float _t = 0; _t < 1; _t += 0.01f) {
                        getGlobalCatmullRomPoint(_t, pos, deriv, t);
                        glVertex3f(pos[0], pos[1], pos[2]);
                    }
                    glEnd();
                    if (config.scene.lighting)
                        glEnable(GL_LIGHTING);
                }

                getGlobalCatmullRomPoint(-1.0f, pos, deriv, t);

                glTranslatef(pos[0], pos[1], pos[2]);

                if (t.align) {
                    float forward[3] = { deriv[0], deriv[1], deriv[2] };
                    float* rotMatrix = getRotMatrix(forward, t.up);
                    glMultMatrixf(rotMatrix);
                    free(rotMatrix);
                }
            } else {
                glTranslatef(t.x, t.y, t.z);
            }
            break;
        case TransformType::Rotate:
            if (t.time > 0.0f) {
                int elapsedInCycle = fmod(globalTimer, t.time * 1000);

                float currRotation = (elapsedInCycle / (t.time * 1000.0f)) * 360.0f;
                glRotatef(currRotation, t.x, t.y, t.z);
            } else {
                glRotatef(t.angle, t.x, t.y, t.z);
            }
            break;
        case TransformType::Scale:
            glScalef(t.x, t.y, t.z);
            break;
        }
    }
}

void drawWithVBOs(const std::vector<GLuint>& vboBuffers,
    const std::vector<GLuint>& vboBuffersNormals,
    const std::vector<GLuint>& vboBuffersTexCoords,
    const std::vector<GLuint>& iboBuffers,
    GroupConfig& group,
    bool depthOnly)
{
    glPushMatrix();
    applyTransformations(group.transforms);

    // the center of the group is determined after the transformations
    if (config.camera.tracking == group.id && !group.name.empty()) {
        updateGroupPosition(group);
    }

    if (depthOnly) {
        float color = group.id / 255.0f;
        glColor3f(color, color, color);
    } else {
        glColor3f(config.group.color.x, config.group.color.y, config.group.color.z);
    }
    glEnableClientState(GL_VERTEX_ARRAY);

    for (const auto& model : group.models) {
        // VBO
        glBindBuffer(GL_ARRAY_BUFFER, vboBuffers[model->modelCore->vboIndex]);
        glVertexPointer(3, GL_FLOAT, 0, 0);

        glEnableClientState(GL_NORMAL_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, vboBuffersNormals[model->modelCore->vboIndex]);
        glNormalPointer(GL_FLOAT, 0, 0);

        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, vboBuffersTexCoords[model->modelCore->vboIndex]);
        glTexCoordPointer(2, GL_FLOAT, 0, 0);

        // IBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboBuffers[model->modelCore->iboIndex]);

        // Set material properties
        if (!depthOnly && config.scene.lighting) {
            glMaterialfv(GL_FRONT, GL_DIFFUSE, model->material.diffuse);
            glMaterialfv(GL_FRONT, GL_AMBIENT, model->material.ambient);
            glMaterialfv(GL_FRONT, GL_SPECULAR, model->material.specular);
            glMaterialfv(GL_FRONT, GL_EMISSION, model->material.emissive);
            glMaterialf(GL_FRONT, GL_SHININESS, model->material.shininess);
        }

        if (config.scene.textures) {
            glBindTexture(GL_TEXTURE_2D, model->texIndex);
        }

        glDrawElements(GL_TRIANGLES,
            model->modelCore->indexCount,
            GL_UNSIGNED_INT,
            0);

        if (config.scene.textures) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Optional: turns off ELEMENT_ARRAY
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    for (auto& child : group.children) {
        drawWithVBOs(vboBuffers, vboBuffersNormals, vboBuffersTexCoords, iboBuffers, *child, depthOnly);
    }

    glPopMatrix();
}
