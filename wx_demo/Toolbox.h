#pragma once

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>

class Toolbox : public wxPanel//Toolbox是自定义的类，拥有wxPanel所有功能
{
public:
    Toolbox(wxWindow* parent);//构造函数
private:
    wxTreeCtrl* m_treeCtrl;// 树状图控件
    wxImageList* m_imageList;// 图标列表

    // 所有图标的索引（在 LoadIcons 里赋值，在 BuildTree 里使用）
    int m_imgFolder;
    int m_imgAdder;
    int m_imgAnd;
    int m_imgButton;
    int m_imgClock;
    int m_imgComparator;
    int m_imgConstant;
    int m_imgCounter;
    int m_imgDelete;
    int m_imgDemux;
    int m_imgDff;
    int m_imgInput;
    int m_imgLed;
    int m_imgMux;
    int m_imgNand;
    int m_imgNor;
    int m_imgNot;
    int m_imgOr;
    int m_imgOutput;
    int m_imgPin;
    int m_imgProbe;
    int m_imgRegister;
    int m_imgSelect;
    int m_imgSplitter;
    int m_imgSubtractor;
    int m_imgText;
    int m_imgWire;
    int m_imgXor;

    void LoadIcons();// 加载所有图标

    void BuildTree();// 构建树结构
};
