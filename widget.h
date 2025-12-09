#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

#include <vtkSmartPointer.h>
#include <vtkResliceImageViewer.h>
#include <vtkImageData.h>
#include <vtkCallbackCommand.h>
#include <vtkCornerAnnotation.h>
#include <vtkCellPicker.h>

#include <itkImage.h>
#include <itkMetaDataObject.h>

#include <string>

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

// VTK 前向声明
class QVTKOpenGLNativeWidget;
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;
class vtkObject;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void onOpenDicom();
    void onOpenMoving();
    void onOrientationToggled();

private:
    enum class Orientation { Axial, Coronal, Sagittal };

    using PixelType = short;
    static constexpr unsigned int Dimension = 3;
    using ImageType = itk::Image<PixelType, Dimension>;

    vtkSmartPointer<vtkImageData> ItkToVtkImage(ImageType *image);
    std::string GetDicomValue(const itk::MetaDataDictionary &dict,
                              const std::string &tagKey) const;
    void registerSliceObserver(vtkResliceImageViewer *viewer,
                               vtkSmartPointer<vtkCallbackCommand> &callback,
                               unsigned long &observerTag);
    void handleSliceInteraction(vtkObject *caller);
    static void SliceChangedCallback(vtkObject* caller,
                                     unsigned long eventId,
                                     void* clientData,
                                     void* callData);

    Ui::Widget *ui;

    // 单视图组件
    QVTKOpenGLNativeWidget *view_fixed;
    QVTKOpenGLNativeWidget *view_moving;
    vtkGenericOpenGLRenderWindow *renderWindow_main;
    vtkGenericOpenGLRenderWindow *renderWindow_moving;
    vtkSmartPointer<vtkResliceImageViewer> m_viewerMain;
    vtkSmartPointer<vtkResliceImageViewer> m_viewerMoving;

    // 角标
    vtkSmartPointer<vtkCornerAnnotation> m_annotMain;
    vtkSmartPointer<vtkCornerAnnotation> m_annotMoving;

    // 回调
    vtkSmartPointer<vtkCallbackCommand> m_sliceCallback;
    unsigned long m_sliceObserverTag;
    unsigned long m_sliceObserverTagMoving;

    // 当前状态
    Orientation m_orientation{Orientation::Axial};
    int m_sliceAxial{0};
    int m_sliceCoronal{0};
    int m_sliceSagittal{0};
    int m_movingSliceAxial{0};
    int m_movingSliceCoronal{0};
    int m_movingSliceSagittal{0};
    bool m_fixedLoaded{false};
    bool m_movingLoaded{false};

    // DICOM 元数据缓存
    std::string m_patientName;
    std::string m_patientID;
    std::string m_patientNameMoving;
    std::string m_patientIDMoving;

    void UpdateAnnotations();
    void setOrientation(Orientation orientation);
    void applySliceForCurrentOrientation(int slice);
    int clampSliceForCurrentViewer(int slice) const;
    int clampSliceForViewer(vtkResliceImageViewer *viewer, int slice) const;
    void setStoredSlice(Orientation orientation, int slice);
    int storedSlice(Orientation orientation) const;
    void setStoredMovingSlice(Orientation orientation, int slice);
    int storedMovingSlice(Orientation orientation) const;
};
#endif // WIDGET_H
