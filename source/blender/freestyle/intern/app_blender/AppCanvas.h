#ifndef ARTCANVAS_H
#define ARTCANVAS_H

#include "../stroke/Canvas.h"

//class AppGLWidget;
class AppCanvas : public Canvas
{
private:
  mutable AppGLWidget *_pViewer;
public:
  AppCanvas();
  AppCanvas(AppGLWidget *iViewer);
  AppCanvas(const AppCanvas& iBrother);
  virtual ~AppCanvas();

  /*! operations that need to be done before a draw */
  virtual void preDraw();
  
  /*! operations that need to be done after a draw */
  virtual void postDraw();
  
  /*! Erases the layers and clears the canvas */
  virtual void Erase(); 
  
  /* init the canvas */
  virtual void init();
  
  /*! Reads a pixel area from the canvas */
  virtual void readColorPixels(int x,int y,int w, int h, RGBImage& oImage) const;
  /*! Reads a depth pixel area from the canvas */
  virtual void readDepthPixels(int x,int y,int w, int h, GrayImage& oImage) const;

  virtual BBox<Vec3r> scene3DBBox() const ;

	// abstract
	virtual void RenderStroke(Stroke*);
	virtual void update();


  /*! accessors */
  virtual int width() const ;
  virtual int height() const ;
  inline const AppGLWidget * viewer() const {return _pViewer;}

  /*! modifiers */
  void setViewer(AppGLWidget *iViewer) ;
};


#endif
