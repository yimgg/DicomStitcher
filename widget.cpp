#include "widget.h"
#include "./ui_widget.h"

#if defined(_MSC_VER) && (_MSC_VER >= 1600)
# pragma execution_character_set("utf-8")
#endif

#include <QFileDialog>
#include <QMessageBox>
#include <QRadioButton>
#include <QString>

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
#include <vtkImageBlend.h>

#include <itkImageSeriesReader.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkOrientImageFilter.h>
#include <itkResampleImageFilter.h>
#include <itkLinearInterpolateImageFunction.h>
#include <itkTranslationTransform.h>
#include <itkTransform.h>
#include <itkMetaDataObject.h>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , view_fixed(nullptr)
    , view_moving(nullptr)
    , view_fusion(nullptr)
    , renderWindow_main(nullptr)
    , renderWindow_moving(nullptr)
    , renderWindow_fusion(nullptr)
    , m_sliceObserverTag(0)
    , m_sliceObserverTagMoving(0)
    , m_fusionOpacity(0.5)
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
    view_fusion = ui->view_fusion;
    
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

    // 融合视图渲染窗口与 viewer（blend 结果）
    renderWindow_fusion = vtkGenericOpenGLRenderWindow::New();
    view_fusion->setRenderWindow(renderWindow_fusion);
    m_viewerFusion = vtkSmartPointer<vtkResliceImageViewer>::New();
    m_viewerFusion->SetRenderWindow(renderWindow_fusion);
    m_viewerFusion->SetupInteractor(renderWindow_fusion->GetInteractor());
    m_viewerFusion->SetSliceOrientationToXY();
    {
        auto dummy = vtkSmartPointer<vtkImageData>::New();
        dummy->SetDimensions(1, 1, 1);
        dummy->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
        std::memset(dummy->GetScalarPointer(), 0, sizeof(unsigned char));
        m_viewerFusion->SetInputData(dummy);
        m_viewerFusion->SetSlice(0);
        m_viewerFusion->SetColorWindow(255.0);
        m_viewerFusion->SetColorLevel(127.0);
    }
    m_annotFusion = vtkSmartPointer<vtkCornerAnnotation>::New();
    m_annotFusion->GetTextProperty()->SetColor(1.0, 1.0, 0.0);
    m_annotFusion->SetMaximumFontSize(14);
    m_viewerFusion->GetRenderer()->AddViewProp(m_annotFusion);

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
    if (renderWindow_fusion) {
        renderWindow_fusion->Delete();
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

    UpdateStatus(QString::fromUtf8("读取 Fixed 序列..."), 5);

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

    UpdateStatus(QString::fromUtf8("方向标准化 (Fixed) ..."), 15);
    ImageType::Pointer oriented = OrientToRAS(reader->GetOutput());

    UpdateStatus(QString::fromUtf8("各向同性重采样 1mm (Fixed) ..."), 35);
    m_fixedResampled = ResampleToIsotropic(oriented.GetPointer(), 1.0);

    // 不读取患者元信息，避免中文编码带来的潜在崩溃
    m_patientName = "N/A";
    m_patientID   = "N/A";

    m_vtkFixed = ItkToVtkImage(m_fixedResampled.GetPointer());
    if (!m_vtkFixed) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("转换图像失败。"));
        return;
    }
    m_movingCoarseOnFixed = nullptr;
    m_vtkFusion = nullptr;

    if (m_viewerMain) {
        m_viewerMain->SetInputData(nullptr);
    }
    m_viewerMain->SetInputData(m_vtkFixed);
    if (m_viewerFusion) {
        m_viewerFusion->SetInputData(m_vtkFixed);
        m_viewerFusion->SetSlice(0);
        m_viewerFusion->Render();
    }
    m_fixedLoaded = true;

    // 计算各个方向的中间切片索引
    const auto region = m_fixedResampled->GetLargestPossibleRegion();
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

    // 默认方向：Axial，更新 viewer
    setOrientation(Orientation::Axial);
    if (ui->radio_orient_axial) {
        ui->radio_orient_axial->setChecked(true);
    }

    // 注册 VTK -> Qt 的交互回调，实现滚轮同步（仅固定视图）
    registerSliceObserver(m_viewerMain, m_sliceCallback, m_sliceObserverTag);
    UpdateAnnotations();
    UpdateStatus(QString::fromUtf8("Fixed 加载完成"), 50);
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

    UpdateStatus(QString::fromUtf8("方向标准化 (Moving) ..."), 70);
    ImageType::Pointer oriented = OrientToRAS(reader->GetOutput());

    UpdateStatus(QString::fromUtf8("各向同性重采样 1mm (Moving) ..."), 80);
    m_movingResampled = ResampleToIsotropic(oriented.GetPointer(), 1.0);

    m_patientNameMoving = "N/A";
    m_patientIDMoving   = "N/A";

    m_vtkMoving = ItkToVtkImage(m_movingResampled.GetPointer());
    if (!m_vtkMoving) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("转换图像失败。"));
        return;
    }

    if (m_viewerMoving) {
        m_viewerMoving->SetInputData(nullptr);
    }
    m_viewerMoving->SetInputData(m_vtkMoving);

    // 只使用轴向方向，取中间切片
    const auto region = m_movingResampled->GetLargestPossibleRegion();
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

    // 标记已加载，注册切片观察者
    m_movingLoaded = true;
    registerSliceObserver(m_viewerMoving, m_sliceCallback, m_sliceObserverTagMoving);

    // 若已加载 fixed，则做几何中心粗对齐并生成融合视图
    if (m_fixedLoaded && m_fixedResampled) {
        UpdateStatus(QString::fromUtf8("粗对齐（几何中心）并生成融合..."), 90);
        using TranslationType = itk::TranslationTransform<double, 3>;
        auto transform = TranslationType::New();
        auto centerF = ComputeCenter(m_fixedResampled.GetPointer());
        auto centerM = ComputeCenter(m_movingResampled.GetPointer());
        TranslationType::OutputVectorType delta;
        delta[0] = centerF[0] - centerM[0];
        delta[1] = centerF[1] - centerM[1];
        delta[2] = centerF[2] - centerM[2];
        transform->Translate(delta);

        m_movingCoarseOnFixed = ResampleToReference(m_movingResampled.GetPointer(),
                                                    m_fixedResampled.GetPointer(),
                                                    transform.GetPointer());

        auto vtkMovingCoarse = ItkToVtkImage(m_movingCoarseOnFixed.GetPointer());
        if (vtkMovingCoarse && m_vtkFixed) {
            auto blender = vtkSmartPointer<vtkImageBlend>::New();
            blender->SetBlendModeToNormal();
            blender->AddInputData(m_vtkFixed);
            blender->AddInputData(vtkMovingCoarse);
            blender->SetOpacity(0, 1.0);
            blender->SetOpacity(1, m_fusionOpacity);
            blender->Update();
            m_vtkFusion = blender->GetOutput();

            if (m_viewerFusion) {
                m_viewerFusion->SetInputData(m_vtkFusion);
                m_viewerFusion->SetSlice(m_sliceAxial);
                if (auto *rendererFusion = m_viewerFusion->GetRenderer()) {
                    rendererFusion->ResetCamera();
                }
                m_viewerFusion->Render();
            }
        }
    }

    setOrientation(m_orientation); // 同步当前方向到 moving / fusion
    UpdateAnnotations();
    UpdateStatus(QString::fromUtf8("Moving 加载完成"), m_fixedLoaded ? 100 : 90);
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

Widget::ImageType::Pointer Widget::OrientToRAS(ImageType *image)
{
    using OrientFilter = itk::OrientImageFilter<ImageType, ImageType>;
    auto orient = OrientFilter::New();
    orient->UseImageDirectionOn();
    orient->SetDesiredCoordinateOrientation(itk::SpatialOrientation::ITK_COORDINATE_ORIENTATION_RAS);
    orient->SetInput(image);
    orient->Update();
    return orient->GetOutput();
}

Widget::ImageType::Pointer Widget::ResampleToIsotropic(ImageType *image, double spacing)
{
    using ResampleFilter = itk::ResampleImageFilter<ImageType, ImageType>;
    using Interpolator = itk::LinearInterpolateImageFunction<ImageType, double>;
    auto resample = ResampleFilter::New();
    auto interp = Interpolator::New();
    resample->SetInput(image);
    resample->SetInterpolator(interp);
    resample->SetDefaultPixelValue(0);

    ImageType::SpacingType newSpacing;
    newSpacing.Fill(spacing);
    resample->SetOutputSpacing(newSpacing);
    resample->SetOutputOrigin(image->GetOrigin());
    resample->SetOutputDirection(image->GetDirection());

    const auto region = image->GetLargestPossibleRegion();
    const auto size = region.GetSize();
    const auto oldSpacing = image->GetSpacing();

    ImageType::SizeType newSize;
    for (unsigned int i = 0; i < 3; ++i) {
        const double physicalLength = oldSpacing[i] * static_cast<double>(size[i] - 1);
        newSize[i] = static_cast<ImageType::SizeType::SizeValueType>(
            std::floor(physicalLength / newSpacing[i] + 0.5)) + 1;
    }
    resample->SetSize(newSize);
    resample->UpdateLargestPossibleRegion();
    return resample->GetOutput();
}

Widget::ImageType::Pointer Widget::ResampleToReference(ImageType *image,
                                                       ImageType *reference,
                                                       itk::Transform<double, 3> *transform)
{
    using ResampleFilter = itk::ResampleImageFilter<ImageType, ImageType>;
    using Interpolator = itk::LinearInterpolateImageFunction<ImageType, double>;
    auto resample = ResampleFilter::New();
    auto interp = Interpolator::New();
    resample->SetInput(image);
    resample->SetInterpolator(interp);
    resample->SetDefaultPixelValue(0);
    resample->SetTransform(transform);

    resample->SetOutputSpacing(reference->GetSpacing());
    resample->SetOutputOrigin(reference->GetOrigin());
    resample->SetOutputDirection(reference->GetDirection());
    resample->SetSize(reference->GetLargestPossibleRegion().GetSize());
    resample->UpdateLargestPossibleRegion();
    return resample->GetOutput();
}

itk::Point<double, 3> Widget::ComputeCenter(ImageType *image) const
{
    itk::Point<double, 3> center;
    if (!image) {
        center.Fill(0.0);
        return center;
    }
    const auto region = image->GetLargestPossibleRegion();
    const auto size = region.GetSize();
    const auto spacing = image->GetSpacing();
    const auto origin = image->GetOrigin();
    const auto direction = image->GetDirection();

    itk::Vector<double, 3> halfExtent;
    for (unsigned int i = 0; i < 3; ++i) {
        halfExtent[i] = spacing[i] * static_cast<double>(size[i] - 1) * 0.5;
    }
    itk::Vector<double, 3> dirHalf = direction * halfExtent;
    for (unsigned int i = 0; i < 3; ++i) {
        center[i] = origin[i] + dirHalf[i];
    }
    return center;
}

void Widget::UpdateStatus(const QString &text, int progress)
{
    if (ui->lbl_status_message) {
        ui->lbl_status_message->setText(text);
    }
    if (progress >= 0 && ui->progress_bar_main) {
        ui->progress_bar_main->setValue(progress);
    }
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
        if (m_vtkFusion && m_viewerFusion) m_viewerFusion->SetSliceOrientationToXY();
        break;
    case Orientation::Coronal:
        m_viewerMain->SetSliceOrientationToXZ();
        if (m_movingLoaded) m_viewerMoving->SetSliceOrientationToXZ();
        if (m_vtkFusion && m_viewerFusion) m_viewerFusion->SetSliceOrientationToXZ();
        break;
    case Orientation::Sagittal:
        m_viewerMain->SetSliceOrientationToYZ();
        if (m_movingLoaded) m_viewerMoving->SetSliceOrientationToYZ();
        if (m_vtkFusion && m_viewerFusion) m_viewerFusion->SetSliceOrientationToYZ();
        break;
    }

    applySliceForCurrentOrientation(storedSlice(orientation));
    int baseSlice = storedSlice(orientation);
    if (m_movingLoaded) {
        int movingSlice = storedMovingSlice(orientation);
        movingSlice = clampSliceForViewer(m_viewerMoving, movingSlice);
        m_viewerMoving->SetSlice(movingSlice);
        if (auto *renderer = m_viewerMoving->GetRenderer()) {
            renderer->ResetCamera();
        }
        m_viewerMoving->Render();
    }
    if (m_vtkFusion && m_viewerFusion) {
        int fusionSlice = clampSliceForViewer(m_viewerFusion, baseSlice);
        m_viewerFusion->SetSlice(fusionSlice);
        if (auto *rendererFusion = m_viewerFusion->GetRenderer()) {
            rendererFusion->ResetCamera();
        }
        m_viewerFusion->Render();
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
