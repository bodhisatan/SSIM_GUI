
// 2017080920TYHDlg.cpp: 实现文件
//

#include "stdafx.h"
#include "2017080920TYH.h"
#include "2017080920TYHDlg.h"
#include "afxdialogex.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define FFSWAP(type, a, b) \
    do                     \
    {                      \
        type SWAP_tmp = b; \
        b = a;             \
        a = SWAP_tmp;      \
    } while (0)
#define FFMIN(a, b) ((a) > (b) ? (b) : (a))

#define BIT_DEPTH 8                      // 位深 使用几位来定位一个像素点 8位的话 像素值范围就是0-255
#define PIXEL_MAX ((1 << BIT_DEPTH) - 1) // 像素值最大值 公式里的L
typedef uint8_t pixel;                   // 8位无符号 表示像素值

char ans[100];
char tot_ans[100000];

/****************************************************************************
 * structural similarity metric
 ****************************************************************************/
static void ssim_4x4x2_core(const pixel *pix1, intptr_t stride1,
	const pixel *pix2, intptr_t stride2,
	int sums[2][4])
{
	// s1 表示 sigma(sigma(a(i, j)))
	// s2 表示 sigma(sigma(b(i, j)))
	// ss 表示 sigma(sigma[a(i, j)^2 + b(i, j)^2])
	// s12 表示 sigma(sigma(a(i, j) * b(i, j)))

	// 函数中计算了一行中两个4x4块，分别将结果存储在sums[0][n]、sums[1][n]中
	int x, y, z;

	for (z = 0; z < 2; z++)
	{
		uint32_t s1 = 0, s2 = 0, ss = 0, s12 = 0;
		for (y = 0; y < 4; y++)
			for (x = 0; x < 4; x++)
			{
				int a = pix1[x + y * stride1];
				int b = pix2[x + y * stride2];
				s1 += a;
				s2 += b;
				ss += a * a;
				ss += b * b;
				s12 += a * b;
			}
		sums[z][0] = s1;
		sums[z][1] = s2;
		sums[z][2] = ss;
		sums[z][3] = s12;
		pix1 += 4;
		pix2 += 4;
	}
}

/*
 * s1：原始像素之和
 * s2：重建像素之和
 * ss：原始像素平方之和+重建像素平方之和
 * s12：原始像素*重建像素的值的和
 */
static float ssim_end1(int s1, int s2, int ss, int s12)
{
	/* Maximum value for 10-bit is: ss*64 = (2^10-1)^2*16*4*64 = 4286582784, which will overflow in some cases.
	 * s1*s1, s2*s2, and s1*s2 also obtain this value for edge cases: ((2^10-1)*16*4)^2 = 4286582784.
	 * Maximum value for 9-bit is: ss*64 = (2^9-1)^2*16*4*64 = 1069551616, which will not overflow. */
#if BIT_DEPTH > 9
	typedef float type;
	static const float ssim_c1 = .01 * .01 * PIXEL_MAX * PIXEL_MAX * 64;
	static const float ssim_c2 = .03 * .03 * PIXEL_MAX * PIXEL_MAX * 64 * 63;
#else
	typedef int type;
	// k1=0.01, k2=0.03
	static const int ssim_c1 = (int)(.01 * .01 * PIXEL_MAX * PIXEL_MAX * 64 * 64 + .5);
	static const int ssim_c2 = (int)(.03 * .03 * PIXEL_MAX * PIXEL_MAX * 64 * 63 + .5);
#endif
	type fs1 = s1;
	type fs2 = s2;
	type fss = ss;
	type fs12 = s12;
	type vars = fss * 64 - fs1 * fs1 - fs2 * fs2;
	type covar = fs12 * 64 - fs1 * fs2;
	return (float)(2 * fs1 * fs2 + ssim_c1) * (float)(2 * covar + ssim_c2) / ((float)(fs1 * fs1 + fs2 * fs2 + ssim_c1) * (float)(vars + ssim_c2));
}

static float ssim_end4(int sum0[5][4], int sum1[5][4], int width)
{
	float ssim = 0.0;
	int i;

	for (i = 0; i < width; i++)
		ssim += ssim_end1(sum0[i][0] + sum0[i + 1][0] + sum1[i][0] + sum1[i + 1][0],
			sum0[i][1] + sum0[i + 1][1] + sum1[i][1] + sum1[i + 1][1],
			sum0[i][2] + sum0[i + 1][2] + sum1[i][2] + sum1[i + 1][2],
			sum0[i][3] + sum0[i + 1][3] + sum1[i][3] + sum1[i + 1][3]);
	return ssim;
}

float ssim_plane(
	pixel *pix1, intptr_t stride1,
	pixel *pix2, intptr_t stride2,
	int width, int height, void *buf, int *cnt)
{
	int z = 0;
	int x, y;
	float ssim = 0.0;
	/*
	 * 按照4x4的块对像素进行处理的。使用sum1保存上一行块的“信息”，sum0保存当前一行块的“信息”
	 * sum0是一个数组指针，其中存储了一个4元素数组的地址
	 * 换句话说，sum0中每一个元素对应一个4x4块的信息（该信息包含4个元素）。
	 *
	 * 4个元素中：
	 * [0]原始像素之和
	 * [1]重建像素之和
	 * [2]原始像素平方之和+重建像素平方之和
	 * [3]原始像素*重建像素的值的和
	 *
	 */
	int(*sum0)[4] = (int(*)[4])buf; // 指向长度为4的数组指针
	int(*sum1)[4] = sum0 + (width >> 2) + 3;
	width >>= 2;
	height >>= 2; // 除以4 因为SSIM计算以4*4为基本单位
	for (y = 1; y < height; y++)
	{
		// 下面这个循环，只有在第一次执行的时候执行2次，处理第1行和第2行的块
		// 后面的都只会执行一次
		for (; z <= y; z++)
		{
			// FFSWAP( (int (*)[4]), sum0, sum1 );
			int(*tmp)[4] = sum0;
			sum0 = sum1;
			sum1 = tmp;

			// 获取4x4块的信息(4个值存于长度为4的一维数组)（这里并没有代入公式计算SSIM结果）
			// 结果存储在sum0中。从左到右每个4x4的块依次存储在sum0[0]，sum0[1]，sum0[2]...
			// 每次前进2个块，通过ssim_4x4x2_core()计算2个4x4块,两个4×4有一半重叠部分
			for (x = 0; x < width; x += 2)
				ssim_4x4x2_core(&pix1[4 * (x + z * stride1)], stride1, &pix2[4 * (x + z * stride2)], stride2, &sum0[x]);
		}
		// sum1是储存上一行的信息，sum0是储存本行的信息，ssim_end4是进行2（line）×4×4×2 2行每行2个4×4的块的单元进行处理
		for (x = 0; x < width - 1; x += 4)
			ssim += ssim_end4(sum0 + x, sum1 + x, FFMIN(4, width - x - 1));
	}
	//     *cnt = (height-1) * (width-1);
	return ssim / ((height - 1) * (width - 1));
}

// 求差的平方和 即ssd
uint64_t ssd_plane(const uint8_t *pix1, const uint8_t *pix2, int size)
{
	uint64_t ssd = 0;
	int i;
	for (i = 0; i < size; i++)
	{
		int d = pix1[i] - pix2[i];
		ssd += d * d;
	}
	return ssd;
}

// PSNR Peak Signal-to-Noise Ratio 峰值信噪比
// ssd Sum of Squared Differences 估算值与估算对象差的平方和
static double ssd_to_psnr(uint64_t ssd, uint64_t denom)
{
	return -10 * log((double)ssd / (denom * 255 * 255)) / log(10);
}

static double ssim_db(double ssim, double weight)
{
	return 10 * (log(weight) / log(10) - log(weight - ssim) / log(10));
}

// 计算输出PSNR和SSIM
static void print_results(uint64_t ssd[3], double ssim[3], int frames, int w, int h)
{
	sprintf(ans, "PSNR Y:%.3f  U:%.3f  V:%.3f  All:%.3f | ",
		ssd_to_psnr(ssd[0], (uint64_t)frames * w * h),
		ssd_to_psnr(ssd[1], (uint64_t)frames * w * h / 4),
		ssd_to_psnr(ssd[2], (uint64_t)frames * w * h / 4),
		ssd_to_psnr(ssd[0] + ssd[1] + ssd[2], (uint64_t)frames * w * h * 3 / 2));

	strcat(tot_ans, ans);

	sprintf(ans, "SSIM Y:%.5f U:%.5f V:%.5f All:%.5f (%.5f)",
		ssim[0] / frames,
		ssim[1] / frames,
		ssim[2] / frames,
		(ssim[0] * 4 + ssim[1] + ssim[2]) / (frames * 6),
		ssim_db(ssim[0] * 4 + ssim[1] + ssim[2], frames * 6));

	strcat(tot_ans, ans);
}


// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CMy2017080920TYHDlg 对话框



CMy2017080920TYHDlg::CMy2017080920TYHDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_MY2017080920TYH_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CMy2017080920TYHDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CMy2017080920TYHDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BUTTON1, &CMy2017080920TYHDlg::OnBnClickedButton1)
	ON_BN_CLICKED(IDC_BUTTON2, &CMy2017080920TYHDlg::OnBnClickedButton2)
	ON_BN_CLICKED(IDC_BUTTON3, &CMy2017080920TYHDlg::OnBnClickedButton3)
END_MESSAGE_MAP()


// CMy2017080920TYHDlg 消息处理程序

BOOL CMy2017080920TYHDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 将“关于...”菜单项添加到系统菜单中。

	// IDM_ABOUTBOX 必须在系统命令范围内。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	// TODO: 在此添加额外的初始化代码

	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

void CMy2017080920TYHDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CMy2017080920TYHDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CMy2017080920TYHDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void CMy2017080920TYHDlg::OnBnClickedButton1()
{
	CFileDialog findFileDlg(
		TRUE,
		_T(".txt"),
		NULL,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		_T("YUV文件 (*.yuv)|*.yuv||")
	);


	// findFileDlg.GetOFN().lpstrInitialDir=strFile.c_str();// 默认目录
	if (IDOK == findFileDlg.DoModal())
	{
		char tempBuff[1024];
		int index = 0, startPos = 0, playTime, iSum, findFlag = 0, iChn = 0;
		CString m_FilePath = findFileDlg.GetPathName();
		SetDlgItemText(IDC_EDIT1, m_FilePath);
	}
}


void CMy2017080920TYHDlg::OnBnClickedButton2()
{
	CFileDialog findFileDlg(
		TRUE,
		_T(".txt"),
		NULL,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		_T("YUV文件 (*.yuv)|*.yuv||")
	);


	// findFileDlg.GetOFN().lpstrInitialDir=strFile.c_str();// 默认目录
	if (IDOK == findFileDlg.DoModal())
	{
		char tempBuff[1024];
		int index = 0, startPos = 0, playTime, iSum, findFlag = 0, iChn = 0;
		CString m_FilePath = findFileDlg.GetPathName();
		SetDlgItemText(IDC_EDIT2, m_FilePath);
	}
}


void CMy2017080920TYHDlg::OnBnClickedButton3()
{
	FILE *f[2];
	uint8_t *buf[2], *plane[2][3];
	int *temp;
	uint64_t ssd[3] = { 0, 0, 0 };
	double ssim[3] = { 0, 0, 0 };
	int frame_size, w, h;
	int frames, seek;
	int i;

	CString video1;
	GetDlgItem(IDC_EDIT1)->GetWindowText(video1);
	CString video2;
	GetDlgItem(IDC_EDIT2)->GetWindowText(video2);

	std::string path1 = CT2A(video1);
	std::string path2 = CT2A(video2);
	// 读入两个文件 长x宽
	f[0] = fopen(path1.c_str(), "rb");
	f[1] = fopen(path2.c_str(), "rb");
	
	CString width_;
	GetDlgItem(IDC_EDIT4)->GetWindowText(width_);
	CString hight_;
	GetDlgItem(IDC_EDIT4)->GetWindowText(hight_);

	std::string w_str = CT2A(width_);
	std::string h_str = CT2A(hight_);
	w = atoi(w_str.c_str());
	h = atoi(h_str.c_str());

	// 一帧的内存大小
	// yuv420格式：先w*h个Y，然后1/4*w*h个U，再然后1/4*w*h个
	frame_size = w * h * 3LL / 2;

	// plane[i][0] Y分量信息
	// plane[i][1] U分量信息
	// plane[i][2] V分量信息
	for (i = 0; i < 2; i++)
	{
		buf[i] = (uint8_t *)malloc(frame_size);
		plane[i][0] = buf[i]; // plane[i][0] = buf[i]
		plane[i][1] = plane[i][0] + w * h;
		plane[i][2] = plane[i][1] + w * h / 4;
	}
	temp = (int *)malloc((2 * w + 12) * sizeof(*temp));

	// 逐帧计算
	for (frames = 0;; frames++)
	{
		uint64_t ssd_one[3]; // Y U V三个向量一帧的ssd结果
		double ssim_one[3];  // Y U V三个向量一帧的ssim结果
		// 分别读入这一帧Y向量的地址，随之也获得了UV向量的起始地址
		if (fread(buf[0], frame_size, 1, f[0]) != 1)
			break;
		if (fread(buf[1], frame_size, 1, f[1]) != 1)
			break;
		for (i = 0; i < 3; i++)
		{
			// i=0 比较Y
			// i=1 比较U
			// i=2 比较V
			// !!的用处 !!i=0(i=0) !!i=1(i>0)
			// 之所以要进行右移，是因为UV每个只占据w*h*1/4，既然wh是右移两位(÷4)，那么单个w单个h只需要右移一位(÷2)
			ssd_one[i] = ssd_plane(plane[0][i], plane[1][i], w * h >> 2 * !!i); // 求像素块1与像素块2的差的平方和
			ssim_one[i] = ssim_plane(plane[0][i], w >> !!i,
				plane[1][i], w >> !!i,
				w >> !!i, h >> !!i, temp, NULL);
			// 累加入总的ssd ssim结果中
			ssd[i] += ssd_one[i];
			ssim[i] += ssim_one[i];
		}

		sprintf(ans, "Frame %d | ", frames);
		strcat(tot_ans, ans);
		print_results(ssd_one, ssim_one, 1, w, h);
		sprintf(ans, "                \r\n");
		strcat(tot_ans, ans);


	}

	if (!frames)
		return;

	sprintf(ans, "Total %d frames | ", frames);
	strcat(tot_ans, ans);

	print_results(ssd, ssim, frames, w, h);

	sprintf(ans, "\n");
	strcat(tot_ans, ans);

	std::string ans_str = tot_ans;
	CString cstr(ans_str.c_str());
	SetDlgItemText(IDC_EDIT3, cstr);

	return;
}
