#include "widget.h"
#include "./ui_widget.h"

#if defined(_MSC_VER) && (_MSC_VER >= 1600)
# pragma execution_character_set("utf-8")
#endif

#include <QFileDialog>
#include <QMessageBox>
#include <QRadioButton>

#include <algorithm>
#include <cstring>
#include <cmath>

// VTK 模块初始化（必须在包含其他 VTK 头文件之前）
#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkInteractionStyle);
VTK_MODULE_INIT(vtkRenderingFreeType);

#include <QVTKOpenGLNativeWidget.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkCommand.h>
#include <vtkCornerAnnotation.h>
#include <vtkTextProperty.h>
#include <vtkCellPicker.h>
#include <vtkInteractorStyleImage.h>
#include <vtkRenderWindowInteractor.h>

#include <itkImageSeriesReader.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkMetaDataObject.h>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , view_fixed(nullptr)
    , view_moving(nullptr)
    , renderWindow_main(nullptr)
    , renderWindow_moving(nullptr)
    , m_sliceObserverTag(0)
    , m_sliceObserverTagMoving(0)
    , m_patientName("N/A")
    , m_patientID("N/A")
    , m_patientNameMoving("N/A")
    , m_patientIDMoving("N/A")
    , m_fixedLoaded(false)
    , m_movingLoaded(false)
{
    ui->setupUi(this);
    connect(ui->btn_load_fixed, &QPushButton::clicked, this, &Widget::onOpenDicom);
    connect(ui->btn_load_moving, &QPushButton::clicked, this, &Widget::onOpenMoving);

    // 获取 UI 中的 QVTKOpenGLNativeWidget（单视图）
    view_fixed = ui->view_fixed;
    view_moving = ui->view_moving;

    // 单视图渲染窗口与 viewer
    renderWindow_main = vtkGenericOpenGLRenderWindow::New();
    view_fixed->setRenderWindow(renderWindow_main);

    m_viewerMain = vtkSmartPointer<vtkResliceImageViewer>::New();
    m_viewerMain->SetRenderWindow(renderWindow_main);
    m_viewerMain->SetupInteractor(renderWindow_main->GetInteractor());
    m_viewerMain->SetSliceOrientationToXY();

    // 占位空图，避免启动阶段窗口渲染时报缺少输入
    {
        auto dummy = vtkSmartPointer<vtkImageData>::New();
        dummy->SetDimensions(1, 1, 1);
        dummy->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
        std::memset(dummy->GetScalarPointer(), 0, sizeof(unsigned char));
        m_viewerMain->SetInputData(dummy);
        m_viewerMain->SetSlice(0);
        m_viewerMain->SetColorWindow(255.0);
        m_viewerMain->SetColorLevel(127.0);
    }

    // 移动视图渲染窗口与 viewer（独立）
    renderWindow_moving = vtkGenericOpenGLRenderWindow::New();
    view_moving->setRenderWindow(renderWindow_moving);

    m_viewerMoving = vtkSmartPointer<vtkResliceImageViewer>::New();
    m_viewerMoving->SetRenderWindow(renderWindow_moving);
    m_viewerMoving->SetupInteractor(renderWindow_moving->GetInteractor());
    m_viewerMoving->SetSliceOrientationToXY();
    {
        auto dummy = vtkSmartPointer<vtkImageData>::New();
        dummy->SetDimensions(1, 1, 1);
        dummy->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
        std::memset(dummy->GetScalarPointer(), 0, sizeof(unsigned char));
        m_viewerMoving->SetInputData(dummy);
        m_viewerMoving->SetSlice(0);
        m_viewerMoving->SetColorWindow(255.0);
        m_viewerMoving->SetColorLevel(127.0);
    }

    // 角标
    m_annotMain = vtkSmartPointer<vtkCornerAnnotation>::New();
    m_annotMain->GetTextProperty()->SetColor(1.0, 1.0, 0.0);
    m_annotMain->SetMaximumFontSize(14);
    m_viewerMain->GetRenderer()->AddViewProp(m_annotMain);
    m_annotMoving = vtkSmartPointer<vtkCornerAnnotation>::New();
    m_annotMoving->GetTextProperty()->SetColor(1.0, 1.0, 0.0);
    m_annotMoving->SetMaximumFontSize(14);
    m_viewerMoving->GetRenderer()->AddViewProp(m_annotMoving);

    // 单选框切换方向
    connect(ui->radio_orient_axial, &QRadioButton::toggled, this, &Widget::onOrientationToggled);
    connect(ui->radio_orient_coronal, &QRadioButton::toggled, this, &Widget::onOrientationToggled);
    connect(ui->radio_orient_sagittal, &QRadioButton::toggled, this, &Widget::onOrientationToggled);
}

Widget::~Widget()
{
    if (renderWindow_main) {
        renderWindow_main->Delete();
    }
    if (renderWindow_moving) {
        renderWindow_moving->Delete();
    }
    delete ui;
}

void Widget::onOpenDicom()
{
    const QString dirPath =
        QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择 DICOM 目录"));
    if (dirPath.isEmpty()) {
        return;
    }

    using ReaderType = itk::ImageSeriesReader<ImageType>;
    auto reader = ReaderType::New();
    auto gdcmIO = itk::GDCMImageIO::New();
    auto fileNames = itk::GDCMSeriesFileNames::New();

    fileNames->SetUseSeriesDetails(true);
    fileNames->AddSeriesRestriction("0008|0021");
    fileNames->SetDirectory(dirPath.toStdString());

    const auto &seriesUIDs = fileNames->GetSeriesUIDs();
    if (seriesUIDs.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("未找到 DICOM 序列。"));
        return;
    }

    reader->SetImageIO(gdcmIO);
    reader->SetFileNames(fileNames->GetFileNames(seriesUIDs.front()));

    try {
        reader->Update();
    } catch (const itk::ExceptionObject &ex) {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
                              QString::fromUtf8("读取失败：%1").arg(QString::fromLocal8Bit(ex.what())));
        return;
    }

    // 不读取患者元信息，避免中文编码带来的潜在崩溃
    m_patientName = "N/A";
    m_patientID   = "N/A";

    vtkSmartPointer<vtkImageData> vtkImage = ItkToVtkImage(reader->GetOutput());
    if (!vtkImage) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("转换图像失败。"));
        return;
    }

    if (m_viewerMain) {
        m_viewerMain->SetInputData(nullptr);
    }
    m_viewerMain->SetInputData(vtkImage);
    m_fixedLoaded = true;

    // 计算各个方向的中间切片索引
    const auto region = reader->GetOutput()->GetLargestPossibleRegion();
    const auto size = region.GetSize();
    const int axialMax = std::max<int>(0, static_cast<int>(size[2]) - 1);
    const int sagittalMax = std::max<int>(0, static_cast<int>(size[0]) - 1);
    const int coronalMax = std::max<int>(0, static_cast<int>(size[1]) - 1);

    m_sliceAxial = std::clamp(static_cast<int>(size[2] / 2), 0, axialMax);
    m_sliceSagittal = std::clamp(static_cast<int>(size[0] / 2), 0, sagittalMax);
    m_sliceCoronal = std::clamp(static_cast<int>(size[1] / 2), 0, coronalMax);

    // 默认窗宽/窗位
    m_viewerMain->SetColorWindow(2000.0);
    m_viewerMain->SetColorLevel(40.0);

    // 默认方向：Axial，更新 viewer 与滑条
    setOrientation(Orientation::Axial);
    if (ui->radio_orient_axial) {
        ui->radio_orient_axial->setChecked(true);
    }

    // 注册 VTK -> Qt 的交互回调，实现滚轮同步（仅固定视图）
    registerSliceObserver(m_viewerMain, m_sliceCallback, m_sliceObserverTag);
    UpdateAnnotations();
}

void Widget::onOpenMoving()
{
    const QString dirPath =
        QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择 DICOM 目录"));
    if (dirPath.isEmpty()) {
        return;
    }

    using ReaderType = itk::ImageSeriesReader<ImageType>;
    auto reader = ReaderType::New();
    auto gdcmIO = itk::GDCMImageIO::New();
    auto fileNames = itk::GDCMSeriesFileNames::New();

    fileNames->SetUseSeriesDetails(true);
    fileNames->AddSeriesRestriction("0008|0021");
    fileNames->SetDirectory(dirPath.toStdString());

    const auto &seriesUIDs = fileNames->GetSeriesUIDs();
    if (seriesUIDs.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("未找到 DICOM 序列。"));
        return;
    }

    reader->SetImageIO(gdcmIO);
    reader->SetFileNames(fileNames->GetFileNames(seriesUIDs.front()));

    try {
        reader->Update();
    } catch (const itk::ExceptionObject &ex) {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
                              QString::fromUtf8("读取失败：%1").arg(QString::fromLocal8Bit(ex.what())));
        return;
    }

    m_patientNameMoving = "N/A";
    m_patientIDMoving   = "N/A";

    vtkSmartPointer<vtkImageData> vtkImage = ItkToVtkImage(reader->GetOutput());
    if (!vtkImage) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("转换图像失败。"));
        return;
    }

    if (m_viewerMoving) {
        m_viewerMoving->SetInputData(nullptr);
    }
    m_viewerMoving->SetInputData(vtkImage);

    // 只使用轴向方向，取中间切片
    const auto region = reader->GetOutput()->GetLargestPossibleRegion();
    const auto size = region.GetSize();
    const int axialMax = std::max<int>(0, static_cast<int>(size[2]) - 1);
    int axialMid = std::clamp(static_cast<int>(size[2] / 2), 0, axialMax);
    const int sagittalMax = std::max<int>(0, static_cast<int>(size[0]) - 1);
    const int coronalMax  = std::max<int>(0, static_cast<int>(size[1]) - 1);
    m_movingSliceAxial    = axialMid;
    m_movingSliceSagittal = std::clamp(static_cast<int>(size[0] / 2), 0, sagittalMax);
    m_movingSliceCoronal  = std::clamp(static_cast<int>(size[1] / 2), 0, coronalMax);

    m_viewerMoving->SetSliceOrientationToXY();
    m_viewerMoving->SetSlice(m_movingSliceAxial);
    m_viewerMoving->SetColorWindow(2000.0);
    m_viewerMoving->SetColorLevel(40.0);
    if (auto *renderer = m_viewerMoving->GetRenderer()) {
        renderer->ResetCamera();
    }
    m_viewerMoving->Render();

    // 标记已加载，注册切片观察者，并让当前方向作用于移动视图
    m_movingLoaded = true;
    registerSliceObserver(m_viewerMoving, m_sliceCallback, m_sliceObserverTagMoving);
    setOrientation(m_orientation); // 同步当前方向到 moving
    UpdateAnnotations();
}

vtkSmartPointer<vtkImageData> Widget::ItkToVtkImage(ImageType *image)
{
    if (!image) {
        return nullptr;
    }

    const auto region = image->GetLargestPossibleRegion();
    const auto size = region.GetSize();
    const auto spacing = image->GetSpacing();
    const auto origin = image->GetOrigin();

    auto vtkImage = vtkSmartPointer<vtkImageData>::New();
    vtkImage->SetDimensions(static_cast<int>(size[0]),
                            static_cast<int>(size[1]),
                            static_cast<int>(size[2]));
    vtkImage->SetSpacing(spacing[0], spacing[1], spacing[2]);
    vtkImage->SetOrigin(origin[0], origin[1], origin[2]);
    vtkImage->AllocateScalars(VTK_SHORT, 1);

    const size_t pixelCount = region.GetNumberOfPixels();
    std::memcpy(vtkImage->GetScalarPointer(),
                image->GetBufferPointer(),
                pixelCount * sizeof(PixelType));

    return vtkImage;
}

std::string Widget::GetDicomValue(const itk::MetaDataDictionary &dict,
                                  const std::string &tagKey) const
{
    using MetaStringType = itk::MetaDataObject<std::string>;
    auto it = dict.Find(tagKey);
    if (it == dict.End()) {
        return "N/A";
    }
    const auto *metaObj = dynamic_cast<const MetaStringType*>(it->second.GetPointer());
    if (!metaObj) {
        return "N/A";
    }
    const std::string &value = metaObj->GetMetaDataObjectValue();
    if (value.empty()) {
        return "N/A";
    }
    return value;
}

void Widget::registerSliceObserver(vtkResliceImageViewer *viewer,
                                   vtkSmartPointer<vtkCallbackCommand> &callback,
                                   unsigned long &observerTag)
{
    if (!viewer) {
        return;
    }

    if (!callback) {
        callback = vtkSmartPointer<vtkCallbackCommand>::New();
        callback->SetCallback(Widget::SliceChangedCallback);
    }

    callback->SetClientData(this);

    if (observerTag != 0) {
        viewer->RemoveObserver(observerTag);
    }

    observerTag = viewer->AddObserver(vtkResliceImageViewer::SliceChangedEvent, callback);
}

void Widget::handleSliceInteraction(vtkObject *caller)
{
    if (!caller) {
        return;
    }

    auto *viewerCaller = vtkResliceImageViewer::SafeDownCast(caller);
    if (!viewerCaller) {
        return;
    }

    if (viewerCaller == m_viewerMain) {
        const int slice = viewerCaller->GetSlice();
        setStoredSlice(m_orientation, slice);
    } else if (m_movingLoaded && viewerCaller == m_viewerMoving) {
        const int slice = viewerCaller->GetSlice();
        setStoredMovingSlice(m_orientation, slice);
    }
    UpdateAnnotations();
}

void Widget::SliceChangedCallback(vtkObject* caller,
                                  unsigned long /*eventId*/,
                                  void* clientData,
                                  void* /*callData*/)
{
    auto *self = static_cast<Widget*>(clientData);
    if (!self) {
        return;
    }
    self->handleSliceInteraction(caller);
}

void Widget::UpdateAnnotations()
{
    // Fixed viewer 注释（仅在已加载后）
    if (m_fixedLoaded && m_viewerMain && m_annotMain) {
        int sliceMin = m_viewerMain->GetSliceMin();
        int sliceMax = m_viewerMain->GetSliceMax();
        int totalSlices = sliceMax - sliceMin + 1;
        if (totalSlices < 1) totalSlices = 1;

        int slice = m_viewerMain->GetSlice() - sliceMin + 1; // 1-based
        slice = std::clamp(slice, 1, totalSlices);

        const char *orientationLabel = "Axial";
        if (m_orientation == Orientation::Coronal) {
            orientationLabel = "Coronal";
        } else if (m_orientation == Orientation::Sagittal) {
            orientationLabel = "Sagittal";
        }

        std::string topLeft = "View: " + std::string(orientationLabel);
        m_annotMain->SetText(0, topLeft.c_str());

        std::string bottomLeft = "Slice: " + std::to_string(slice) +
                                 " / " + std::to_string(totalSlices);
        m_annotMain->SetText(1, bottomLeft.c_str());

        double w = m_viewerMain->GetColorWindow();
        double l = m_viewerMain->GetColorLevel();
        std::string bottomRight = "W: " + std::to_string(static_cast<int>(w)) +
                                  "  L: " + std::to_string(static_cast<int>(l));
        m_annotMain->SetText(2, bottomRight.c_str());

        m_viewerMain->Render();
    }

    // Moving viewer 注释（仅在加载后）
    if (m_movingLoaded && m_viewerMoving && m_annotMoving) {
        int sliceMin = m_viewerMoving->GetSliceMin();
        int sliceMax = m_viewerMoving->GetSliceMax();
        int totalSlices = sliceMax - sliceMin + 1;
        if (totalSlices < 1) totalSlices = 1;

        int slice = m_viewerMoving->GetSlice() - sliceMin + 1;
        slice = std::clamp(slice, 1, totalSlices);

        const char *orientationLabel = "Axial";
        if (m_orientation == Orientation::Coronal) {
            orientationLabel = "Coronal";
        } else if (m_orientation == Orientation::Sagittal) {
            orientationLabel = "Sagittal";
        }

        std::string topLeft = "View: " + std::string(orientationLabel);
        m_annotMoving->SetText(0, topLeft.c_str());

        std::string bottomLeft = "Slice: " + std::to_string(slice) +
                                 " / " + std::to_string(totalSlices);
        m_annotMoving->SetText(1, bottomLeft.c_str());

        double w = m_viewerMoving->GetColorWindow();
        double l = m_viewerMoving->GetColorLevel();
        std::string bottomRight = "W: " + std::to_string(static_cast<int>(w)) +
                                  "  L: " + std::to_string(static_cast<int>(l));
        m_annotMoving->SetText(2, bottomRight.c_str());

        m_viewerMoving->Render();
    }
}

void Widget::onOrientationToggled()
{
    if (ui->radio_orient_axial && ui->radio_orient_axial->isChecked()) {
        setOrientation(Orientation::Axial);
    } else if (ui->radio_orient_coronal && ui->radio_orient_coronal->isChecked()) {
        setOrientation(Orientation::Coronal);
    } else if (ui->radio_orient_sagittal && ui->radio_orient_sagittal->isChecked()) {
        setOrientation(Orientation::Sagittal);
    }
}

void Widget::setOrientation(Orientation orientation)
{
    if (!m_viewerMain) {
        return;
    }

    m_orientation = orientation;

    switch (orientation) {
    case Orientation::Axial:
        m_viewerMain->SetSliceOrientationToXY();
        if (m_movingLoaded) m_viewerMoving->SetSliceOrientationToXY();
        break;
    case Orientation::Coronal:
        m_viewerMain->SetSliceOrientationToXZ();
        if (m_movingLoaded) m_viewerMoving->SetSliceOrientationToXZ();
        break;
    case Orientation::Sagittal:
        m_viewerMain->SetSliceOrientationToYZ();
        if (m_movingLoaded) m_viewerMoving->SetSliceOrientationToYZ();
        break;
    }

    applySliceForCurrentOrientation(storedSlice(orientation));
    if (m_movingLoaded) {
        int movingSlice = storedMovingSlice(orientation);
        movingSlice = clampSliceForViewer(m_viewerMoving, movingSlice);
        m_viewerMoving->SetSlice(movingSlice);
        if (auto *renderer = m_viewerMoving->GetRenderer()) {
            renderer->ResetCamera();
        }
        m_viewerMoving->Render();
    }

    if (auto *renderer = m_viewerMain->GetRenderer()) {
        renderer->ResetCamera();
    }
    m_viewerMain->Render();
    UpdateAnnotations();
}

void Widget::applySliceForCurrentOrientation(int slice)
{
    if (!m_viewerMain) {
        return;
    }
    const int clamped = clampSliceForCurrentViewer(slice);
    setStoredSlice(m_orientation, clamped);
    m_viewerMain->SetSlice(clamped);
    m_viewerMain->Render();
    UpdateAnnotations();
}

int Widget::clampSliceForViewer(vtkResliceImageViewer *viewer, int slice) const
{
    if (!viewer) {
        return slice;
    }
    int minVal = viewer->GetSliceMin();
    int maxVal = viewer->GetSliceMax();
    if (slice < minVal) return minVal;
    if (slice > maxVal) return maxVal;
    return slice;
}

int Widget::clampSliceForCurrentViewer(int slice) const
{
    if (!m_viewerMain) {
        return slice;
    }
    int minVal = m_viewerMain->GetSliceMin();
    int maxVal = m_viewerMain->GetSliceMax();
    if (slice < minVal) return minVal;
    if (slice > maxVal) return maxVal;
    return slice;
}

void Widget::setStoredSlice(Orientation orientation, int slice)
{
    switch (orientation) {
    case Orientation::Axial:
        m_sliceAxial = slice;
        break;
    case Orientation::Coronal:
        m_sliceCoronal = slice;
        break;
    case Orientation::Sagittal:
        m_sliceSagittal = slice;
        break;
    }
}

int Widget::storedSlice(Orientation orientation) const
{
    switch (orientation) {
    case Orientation::Axial:
        return m_sliceAxial;
    case Orientation::Coronal:
        return m_sliceCoronal;
    case Orientation::Sagittal:
        return m_sliceSagittal;
    default:
        return 0;
    }
}

void Widget::setStoredMovingSlice(Orientation orientation, int slice)
{
    switch (orientation) {
    case Orientation::Axial:
        m_movingSliceAxial = slice;
        break;
    case Orientation::Coronal:
        m_movingSliceCoronal = slice;
        break;
    case Orientation::Sagittal:
        m_movingSliceSagittal = slice;
        break;
    }
}

int Widget::storedMovingSlice(Orientation orientation) const
{
    switch (orientation) {
    case Orientation::Axial:
        return m_movingSliceAxial;
    case Orientation::Coronal:
        return m_movingSliceCoronal;
    case Orientation::Sagittal:
        return m_movingSliceSagittal;
    default:
        return 0;
    }
}
