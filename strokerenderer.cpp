#include "strokerenderer.h"
#include "timeline.h"
#include <limits>
#include <qmath.h>
#include <vector>


#define VERTEX_COORD_SIZE 4
#define N_SPRITES 10000000

StrokeRenderer* StrokeRenderer::si;

StrokeRenderer::StrokeRenderer()
{
    si = this;
}

void StrokeRenderer::windowSizeChanged(int width, int height)
{
    qDebug() << "canvas resized to:" << width << height;

    canvasSize.setX(width);
    canvasSize.setY(height);

    zoom = canvasRatio * canvasSize.x() / canvasSize.y();
    scroll = viewportYStart * zoom * 2.0f;

    strokeShader.shaderProgram.bind();
    strokeShader.shaderProgram.setUniformValue(strokeZoomAndScrollLoc, zoom, scroll);

    pickingShader.shaderProgram.bind();
    pickingShader.shaderProgram.setUniformValue(pickingZoomAndScrollLoc, zoom, scroll);

    selectionRectShader.shaderProgram.bind();
    selectionRectShader.shaderProgram.setUniformValue(rectZoomAndScrollLoc, zoom, scroll);

    scrollBar->setPageStep(scrollBarSize / zoom);
    scrollBar->setRange(0, scrollBarSize - scrollBar->pageStep());

    Canvas::si->redrawRequested = true;
}

void StrokeRenderer::setViewportYStart(float value)
{
    viewportYStart = value / scrollBarSize;

    scroll = viewportYStart * zoom * 2.0f;

    strokeShader.shaderProgram.bind();
    strokeShader.shaderProgram.setUniformValue(strokeZoomAndScrollLoc, zoom, scroll);

    pickingShader.shaderProgram.bind();
    pickingShader.shaderProgram.setUniformValue(pickingZoomAndScrollLoc, zoom, scroll);

    selectionRectShader.shaderProgram.bind();
    selectionRectShader.shaderProgram.setUniformValue(rectZoomAndScrollLoc, zoom, scroll);

    Canvas::si->redrawRequested = true;
}

void StrokeRenderer::init()
{
    // Start OpenGL
    INIT_OPENGL_FUNCTIONS();

    // Get a reference to the scrollBar widget - just for cleaner code
    scrollBar = MainWindow::si->getCanvasScrollBar();


    // Setup stroke vertex buffers
    glGenBuffers(1, &verticesId);
    glBindBuffer(GL_ARRAY_BUFFER, verticesId);
    glBufferData(GL_ARRAY_BUFFER, N_SPRITES, NULL, GL_STREAM_DRAW);

    // Setup canvas geometry
    glGenBuffers(1, &rectId);
    glBindBuffer(GL_ARRAY_BUFFER, rectId);
    glBufferData(GL_ARRAY_BUFFER, 4, NULL, GL_DYNAMIC_DRAW);


    // Setup stroke shader
    strokeShader.init(QString("stroke"), {"inTexCoord"});
    strokeShader.shaderProgram.bind();
    strokeColorLoc = strokeShader.shaderProgram.uniformLocation("strokeColor");
    strokeZoomAndScrollLoc = strokeShader.shaderProgram.uniformLocation("zoomAndScroll");
    strokeMatrix = strokeShader.shaderProgram.uniformLocation("manipulation");

    // Setup picking shader
    pickingShader.init(QString("picking"), {"inTexCoord"});
    pickingShader.shaderProgram.bind();
    pickingColorLoc = pickingShader.shaderProgram.uniformLocation("strokeColor");
    pickingZoomAndScrollLoc = pickingShader.shaderProgram.uniformLocation("zoomAndScroll");
    pickingMatrix = strokeShader.shaderProgram.uniformLocation("manipulation");

    // Setup canvas shader
    canvasShader.init(QString("canvas"), {});
    canvasShader.shaderProgram.bind();
    samplerRectLoc = canvasShader.shaderProgram.uniformLocation("sampler");
    canvasShader.shaderProgram.setUniformValue(samplerRectLoc, 0);

    // Setup selection rect shader
    selectionRectShader.init(QString("selectionRect"), {});
    selectionRectShader.shaderProgram.bind();
    rectZoomAndScrollLoc = selectionRectShader.shaderProgram.uniformLocation("zoomAndScroll");


    // Get cursor image
    QImage cursor = QGLWidget::convertToGLFormat( QImage(":/icons/icons/cursor32.png") );

    glGenTextures( 1, &cursorTexture );
    glBindTexture( GL_TEXTURE_2D, cursorTexture );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, cursor.width(), cursor.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, cursor.bits() );

    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
}

int StrokeRenderer::addStroke(const QLineF &strokeLine)
{
    float w = strokeLine.x2() - strokeLine.x1();
    float h = strokeLine.y2() - strokeLine.y1();

    float dist = sqrt(h*h * canvasRatioSquared + w*w);

    float i;
    for (i=extraDist; i<dist; i+=spriteSpacing)
    {
        addStrokeSprite(strokeLine.x1() + w / dist * i,
                        strokeLine.y1() + h / dist * i );
    }
    extraDist = i-dist;

    return spriteCounter;
}


int StrokeRenderer::addPoint(const QPointF &strokePoint)
{
    addStrokeSprite(strokePoint.x(), strokePoint.y());

    extraDist = spriteSpacing;

    return spriteCounter;
}


static void IDtoColor(float &r, float &g, float &b, const int &ID)
{
    b = ( (ID >> 16) & 0xFF ) / 255.0f;
    g = ( (ID >> 8 ) & 0xFF ) / 255.0f;
    r = ( (ID      ) & 0xFF ) / 255.0f;
}


static int colorToId(int r, int g, int b)
{
    return r +
           ( (b << 8) & 0xFFFF ) +
           ( (g << 16) & 0xFFFFFF );
}


void StrokeRenderer::processPicking()
{
    int x = Canvas::si->penIntPos.x();
    int y = Canvas::si->h - Canvas::si->penIntPos.y();

    GLubyte pickedPixel[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pickedPixel);

    qDebug() << x << y << colorToId(pickedPixel[0], pickedPixel[1], pickedPixel[2]);

    Event::setActiveID(colorToId(pickedPixel[0], pickedPixel[1], pickedPixel[2]), Canvas::si->penPos);
}

void StrokeRenderer::renderSelectionRect(QRectF rect)
{
    qDebug() << rect;

    float padding = zoom * (1.0 / 300.0);

    float posArray[] = {rect.left() - padding,  rect.bottom() - padding,
                        rect.left() - padding,  rect.top() + padding,
                        rect.right() + padding, rect.top() + padding,
                        rect.right() + padding, rect.bottom() - padding};

    glUseProgram(selectionRectShader.pId);

    glBindBuffer(GL_ARRAY_BUFFER, rectId);
    glBufferData(GL_ARRAY_BUFFER, 32, posArray, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, false, 8, (void*)0);
    glEnableVertexAttribArray(0);

    glDrawArrays(GL_LINE_LOOP, 0, 4);

    glDisableVertexAttribArray(0);
}


int StrokeRenderer::getCurrentSpriteCounter()
{
    return spriteCounter;
}


void StrokeRenderer::addStrokeSprite(float x, float y)
{
    if (x < SHRT_MIN || x > SHRT_MAX || y < SHRT_MIN || y > SHRT_MAX) return;

    if(spriteCounter < N_SPRITES)
    {
        GLshort posArray[] = {(GLshort)x, (GLshort)y};

        glBindBuffer(GL_ARRAY_BUFFER, verticesId);
        glBufferSubData(GL_ARRAY_BUFFER, spriteCounter*VERTEX_COORD_SIZE, VERTEX_COORD_SIZE, posArray);

        spriteCounter++;
    }
}


void StrokeRenderer::drawCursor()
{
    glBindTexture(GL_TEXTURE_2D, cursorTexture);
    drawTexturedRect( Timeline::si->cursorPosition.x() / SHRT_MAX - 32.0f / canvasSize.x(),
                     (Timeline::si->cursorPosition.y() / SHRT_MAX * zoom - zoom + 1.0f + scroll) - 32.0f / canvasSize.y(),
                      64.0f / canvasSize.x(),
                      64.0f / canvasSize.y() );
}


void StrokeRenderer::drawStrokeSpritesRange(int from, int to, float r, float g, float b, float ptSize, QMatrix4x4 transform, int ID)
{
    if (Canvas::si->pickingRequested) // Render for picking
    {
        IDtoColor(r, g, b, ID);

        glPointSize( (ptSize + pickingSizeAdjustment) * (canvasSize.x() * normalSizeAdjustment));

        glUseProgram(pickingShader.pId);
        pickingShader.shaderProgram.setUniformValue(pickingColorLoc, r, g, 0);
        pickingShader.shaderProgram.setUniformValue(pickingMatrix, transform);

        glBindBuffer(GL_ARRAY_BUFFER, verticesId);
        glVertexAttribPointer(0, 2, GL_SHORT, true, VERTEX_COORD_SIZE, 0);

        glEnableVertexAttribArray(0);

        glDrawArrays(GL_POINTS, from, to - from);

        glDisableVertexAttribArray(0);
    }
    else // Render normally
    {
        glPointSize(ptSize * (canvasSize.x() * normalSizeAdjustment));

        glUseProgram(strokeShader.pId);
        strokeShader.shaderProgram.setUniformValue(strokeColorLoc, r, g, b);
        strokeShader.shaderProgram.setUniformValue(strokeMatrix, transform);

        glBindBuffer(GL_ARRAY_BUFFER, verticesId);
        glVertexAttribPointer(0, 2, GL_SHORT, true, VERTEX_COORD_SIZE, 0);

        glEnableVertexAttribArray(0);

        glDrawArrays(GL_POINTS, from, to - from);

        glDisableVertexAttribArray(0);
    }
}


void StrokeRenderer::drawTexturedRect(float x, float y, float w, float h)
{
    float posArray[] = {x,   y,   0, 1,
                        x,   y+h, 0, 0,
                        x+w, y+h, 1, 0,
                        x+w, y,	  1, 1};

    glUseProgram(canvasShader.pId);

    glBindBuffer(GL_ARRAY_BUFFER, rectId);
    glBufferData(GL_ARRAY_BUFFER, 64, posArray, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, false, 16, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, false, 16, (void*)8);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}
