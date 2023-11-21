#include "ProcessManager.h"
#include "utils/parseutil.h"
#include "myitem.h"
#include <QTimer>
#include <chrono>
#include <QFile>
#include <QFileDialog>
#include <QDebug>
#include <QStandardItemModel>

ProcessManager::ProcessManager(CPUPara*aCpuPara,const std::string&preparedfile,ScrollableCardContainer*abackcon,
                               ScrollableCardContainer*areadycon,
                               ScrollableCardContainer*asuspendcon,QTableWidget*aprocessingTable,QTableWidget* aprocessTable,QListView*adetailList,QObject*parent):QObject(parent),preparedfile(preparedfile),cpupara(aCpuPara){
    takeContainer(abackcon,areadycon,asuspendcon,aprocessingTable,aprocessTable,adetailList);
    initBackQueue();
    timer=new QTimer;
    connect(timer,&QTimer::timeout,this,&ProcessManager::invokeFunc);
}
ProcessManager::~ProcessManager(){
    for(auto &it:processes){
        delete it.second;
    }
    processes.clear();
}
void ProcessManager::registerAProcess(proc*process){
    processes[process->name]=process;
    addDetailInfo(process);
}
proc* ProcessManager::getProcessInfo(const QString&name){
    return processes[name];
}
void ProcessManager::initBackQueue(){
    std::vector<proc*>vec;
    readfile(preparedfile,vec,this,this->checkPara);
    int num=0;
    for(proc*& item:vec){
        backqueue.push(std::move(item));
        registerAProcess(item);
        ++num;
    }
    backcon->updateByQueue(backqueue);
}
void ProcessManager::takeContainer(ScrollableCardContainer*abackcon,
                   ScrollableCardContainer*areadycon,
                   ScrollableCardContainer*asuspendcon,
                   QTableWidget*aprocessingTable,
                   QTableWidget* aprocessTable,
                   QListView*detailList){
    this->backcon=abackcon;
    this->readycon=areadycon;
    this->suspendcon=asuspendcon;
    connect(areadycon,&ScrollableCardContainer::CardClicked,this,&ProcessManager::addPreSuspendSet);
    connect(areadycon,&ScrollableCardContainer::BreakCardClicked,this,&ProcessManager::removeFromPreSuspendSet);
    connect(suspendcon,&ScrollableCardContainer::CardClicked,this,&ProcessManager::addPreReleaseSet);
    connect(suspendcon,&ScrollableCardContainer::BreakCardClicked,this,&ProcessManager::removeFromPreReleaseSet);
    this->processingTable=aprocessingTable;
    this->processTable=aprocessTable;
    this->detailList=detailList;
}
int ProcessManager::backSize(){return backqueue.size();}
int ProcessManager::readySize(){return readyqueue.size();}
int ProcessManager::suspendSize(){return suspendqueue.size();}
void ProcessManager::setServeTime(const int&time){
    serveTime=time;
}
//参数-1记为全部转移
void ProcessManager::backToReady(const int&n){
    int num=(n==-1)?INT_MAX:n;//即使这里提出想要尽可能多读入的请求，也不能多于并行道数
    int i=0;
    while(i<num&&readyqueue.size()<cpupara->getcpuParallelThreads()&&(!backqueue.empty())&&(!backcon->empty())){
        readyqueue.push(backqueue.top());
        backqueue.pop();
        backcon->pop();
        ++i;
    }
    readycon->updateByQueue(readyqueue);
}
//参数-1记为全部转移
void ProcessManager::readyToSuspend(const int&n){
    int num=(n==-1)?INT_MAX:n;
    int i=0;
    while(i<num&&!readyqueue.empty()&&!readycon->empty()){
        suspendqueue.push_back(readyqueue.top());
        readyqueue.pop();
        readycon->pop();
        ++i;
    }
    suspendcon->updateByQueue(suspendqueue);
}
//参数-1记为全部转移
void ProcessManager::suspendToReady(const int&n){
    int num=(n==-1)?INT_MAX:n;
    int i=0;
    while(i<num&&!suspendqueue.empty()&&!suspendcon->empty()){
        readyqueue.push(suspendqueue[0]);
        suspendqueue.pop_front();
        suspendcon->pop();
        ++i;
    }
    readycon->updateByQueue(readyqueue);
}
void ProcessManager::begin(){
    kickOnNext(1,sysTime);
}
void ProcessManager::pause(){
    timer->stop();
}
void ProcessManager::continueProcess(){
    timer->setSingleShot(true);
    timer->start();
}
void ProcessManager::reloadReady(){
    reloadedJustNow=true;
    backToReady(-1);
    kickOnNext(2,serveTime);
}
void ProcessManager::processingBackToReady(){
    for(auto it:servingqueue){
        readyqueue.push(it);
    }
//    for (auto it = servingqueue.begin(); it != servingqueue.end(); ++it) {
//        readyqueue.push(std::move(*it));
//    }
    servingqueue.clear();  // 清空 servingqueue
    readycon->updateByQueue(readyqueue);
}
void ProcessManager::serveProcess(){
    clearProcessingTable();
    //如果到了下一次，该有进程needTime还有，则再次希望先装填，停顿sysTime后，再进行下次cpu选择。
    if(!servingqueue.empty()){
        processingBackToReady();
        //这里的更新选中状态应该等这个队列定下来之后再来更新，要不也是徒劳，因为我们的策略：在视图上，能修改就不删除重新创建，所以一个card指针不变，但是内容变了，所以我们要执行扫描更新，而不能依靠指针更新
        readycon->updateClickedState(preSuspendSet);
        kickOnNext(2,serveTime);
        return;
    }else if(reloadedJustNow){
        //这里还要判断reloadedJustNow就是为了减少不必要的update，因为updateClickedState复杂度是O(mn)，所以节省一些
        reloadedJustNow=false;
        readycon->updateClickedState(preSuspendSet);
    }
    int i=1;
    proc*process=nullptr;
    while(!readyqueue.empty()&& i<=cpupara->getCoreCount()){
        if(readyqueue.size()!=readycon->size()){
            std::cout<<"[严重]readyqueue.size()!=readycon->size()   已主动结束"<<std::endl;/*debug------------------------如果实在没有问题就删掉*/
            exit(1);
        }
        process=readyqueue.top();
        process->decNeedTime();
        process->changePriority(process->priority+1);
        //若进程被选入将要执行，所以它的被选中状态要抹除
        if(preSuspendSet.count(process)>0){
            preSuspendSet.erase(process);
            //readycon->eraseIfClicked(process->name);
        }
        drawACPUProcess(i,process);
        if(!process->isOver())servingqueue.push_back(process);
        readyqueue.pop();
        readycon->pop();
        ++i;
    }
    if(readyqueue.size()+servingqueue.size()>=cpupara->getCoreCount()){
        kickOnNext(2,serveTime);
    }
    else{
        kickOnNext(1,serveTime);//reloadReady()调用过程中仍然会再次调用这个函数，而这个函数再开始就会装填servingqueue,所以不用担心;
    }
}
void ProcessManager::drawACPUProcess(const int&coreid,const proc*process){
    if(coreid>cpupara->getCoreCount()){
        std::cout<<"\"正在执行区\"行数错误！"<<std::endl;
        exit(1);
    }
    QTableWidgetItem* item;
    int i=1;
    QString qstr;
    do{
       item = processingTable->item(coreid-1, i);
        if(!item){
           std::cout<<"\"正在执行区\"出现行空指针"<<std::endl;
            exit(1);
       }
       switch(i){
        case 1:
            qstr=process->name;
            break;
        case 2:
            qstr=QString::number(process->priority);
            break;
        default:
            qstr=QString::number(process->needTime);
            break;
       }
        item->setText(qstr);
       ++i;
    }while(i<=3);
    isProcessingTableClean=false;
}
void ProcessManager::clearProcessingTable(){
    if(isProcessingTableClean) return;
    QTableWidgetItem* item;
    for(int i=0;i<cpupara->getCoreCount();++i){
       for(int j=1;j<4;++j){
            item = processingTable->item(i, j);
            if(item)item->setText("");
       }
    }
    isProcessingTableClean=true;
}
void ProcessManager::clearProcessTable(int rownum){
    int num=(rownum==-1)?INT_MAX:rownum;
    QTableWidgetItem* item;
    for(int i=0;i<num;++i){
       for(int j=0;j<3;++j){
            item = processTable->item(i, j);
            if(item)item->setText("");
            else goto breaklabel;
       }
    }
 breaklabel:{};
}
void ProcessManager::invokeFunc(){
    counting=false;
    switch (funcCode) {
    case 1:
        reloadReady();
        break;
    case 2:
        serveProcess();
        break;
    case 3:
        processingBackToReady();
        break;
    default:
        break;
    }
}
void ProcessManager::kickOnNext(const int&code,const int&interval){
    while(counting){};
    counting=true;
    funcCode=code;
    timer->setSingleShot(true); // 设置定时器为一次性定时器
    timer->start(interval);
}
void ProcessManager::addRows(const int& num){
    if(num<1)return;
    proc*item;
    for(int i=1;i<=num;++i){
        item=readArow(i);
        if(!item)continue;
        registerAProcess(item);
        backqueue.push(item);
    }
    backcon->updateByQueue(backqueue);
    clearProcessTable(num);
}
void ProcessManager::readRowsFromFile(){
    // 打开记事本等文本编辑器
    QString filePath = QFileDialog::getOpenFileName(nullptr,"选择CSV文件", "", "CSV Files (*.csv)");
    std::vector<proc*>tmp;
    readfile(filePath.toStdString(),tmp,this,this->checkPara);
    for(auto& it:tmp){
        registerAProcess(it);
        backqueue.push(std::move(it));
    }
    backcon->updateByQueue(backqueue);
}
proc* ProcessManager::checkPara(const QString&name,const QString&time,const QString&pri){
    if(name==""||processes.count(name)>0)return nullptr;
    bool trans=true; int atime; int apri;
    atime=time.toInt(&trans);
    if(!trans||atime<1)return nullptr;
    apri=pri.toInt(&trans);
    if(!trans)return nullptr;
    return new proc(name,atime,apri);
}
//所有的行在QT中是以0开始，这里把第一行的rownum=1
proc* ProcessManager::readArow(const int&rownum){
    if(rownum<1){
        std::cout<<"\"添加执行区\"行数错误！"<<std::endl;
        exit(1);
    }
    QTableWidgetItem* item;
    QString name;QString priority;QString time;
    int i=0;
    do{
        item = processTable->item(rownum-1, i);
        if(!item){
            std::cout<<"\"添加执行区\"出现行空指针"<<std::endl;
            exit(1);
        }
        switch(i){
        case 0:
            name=item->text();
            break;
        case 1:
            priority=item->text();
            break;
        default:
            time=item->text();
            break;
        }
        ++i;
    }while(i<3);
    return checkPara(name,time,priority);
}
void ProcessManager::addPreSuspendSet(const QString&name){
    proc*process=processes[name];
    if(!process){
        std::cout<<"[严重] [where]:ProcessManager::addPreSuspendSet 选中的process哈希表中不存在！已主动结束"<<std::endl;
        exit(1);
    }
    preSuspendSet.insert(process);
}
void ProcessManager::removeFromPreSuspendSet(const QString&name){
    proc*process=processes[name];
    if(!process){
        std::cout<<"[严重] [where]:PProcessManager::removeFromPreSuspendSet 选中的process哈希表中不存在！"<<std::endl;
        exit(1);
    }
    auto it =preSuspendSet.find(process);
    if(it==preSuspendSet.end()){
        std::cout<<"[严重] [where]:PProcessManager::removeFromPreSuspendSet 试图解除一个 选择挂起队列 中不存在的进程！"<<std::endl;
        exit(1);
    }
    preSuspendSet.erase(it);
}
void ProcessManager::addPreReleaseSet(const QString&name){
    proc*process=processes[name];
    if(!process){
        std::cout<<"[严重] [where]:ProcessManager::addPreReleaseSet 选中的process哈希表中不存在！已主动结束"<<std::endl;
        exit(1);
    }
    preReleaseSet.insert(process);
}
void ProcessManager::removeFromPreReleaseSet(const QString&name){
    proc*process=processes[name];
    if(!process){
        std::cout<<"[严重] [where]:ProcessManager::removeFromPreReleaseSet 选中的process哈希表中不存在！"<<std::endl;
        exit(1);
    }
    auto it =preReleaseSet.find(process);
    if(it==preReleaseSet.end()){
        std::cout<<"[严重] [where]:PProcessManager::removeFromPreSuspendSet 试图解除一个 选择挂起队列 中不存在的进程！"<<std::endl;
        exit(1);
    }
    preReleaseSet.erase(it);
}
void ProcessManager::addSuspendQueue(){
    //当预备挂起队列已经被证实添加到挂起队列，但是考虑到我们更新card的策略是，能修改就不删除，所以有必要将队列一切仍然可能处于clicke的组件，设置为非click
    readycon->setAllNotClicked();
    for(auto it: preSuspendSet){
        suspendqueue.push_back(it);
        readyqueue.remove(it);
    }
    preSuspendSet.clear();  // 清空 servingqueue
    readycon->updateByQueue(readyqueue);
    suspendcon->updateByQueue(suspendqueue);
}
void ProcessManager::addSuspendQueueFromProcessing(){
    // 获取用户选择的行
    QList<QTableWidgetItem*> selectedItems = processingTable->selectedItems();
    //我这不是多此一举，只是selectedItems()无论你选中几行，都会返回 行数*选中数 个元素，欣慰的是，有重复但没有错失，但是要过滤重复。
    QSet<int> selectedRows;
    // 获取所有选中行的索引
    foreach (QTableWidgetItem* item, selectedItems)selectedRows.insert(item->row());
    proc*p=nullptr;
    foreach(const int&row,selectedRows){
        p=processes[processingTable->item(row,1)->text()];
        if(!p) return;
        ++(p->needTime);
        suspendqueue.push_back(p);
        auto iter=std::find(servingqueue.begin(),servingqueue.end(),p);
        if(iter!=servingqueue.end())servingqueue.erase(iter);
    }
    suspendcon->updateByQueue(suspendqueue);
    processingTable->clearSelection();
}
void ProcessManager::releaseFromSuspendQueue(const int&code){
    //当预备挂起队列已经被证实添加到挂起队列，但是考虑到我们更新card的策略是，能修改就不删除，所以有必要将队列一切仍然可能处于clicke的组件，设置为非click
    suspendcon->setAllNotClicked();
    if(code==-1){
        for(auto it:suspendqueue){
            readyqueue.push(it);
        }
        suspendqueue.clear();
    }else{
        for(auto it: preReleaseSet){
            readyqueue.push(it);
            auto iter=std::find(suspendqueue.begin(),suspendqueue.end(),it);
            if(iter==suspendqueue.end()){
                std::cout<<"[严重] [where]:ProcessManager::releaseFromSuspendQueue 试图解除一个 选择释放挂起队列 中不存在的进程！"<<std::endl;
                exit(1);
            }
            suspendqueue.erase(iter);
        }
    }
    preReleaseSet.clear();  // 清空 servingqueue
    readycon->updateByQueue(readyqueue);
    suspendcon->updateByQueue(suspendqueue);
}
void ProcessManager::addDetailInfo(proc*process){
    MyItem*item=new MyItem(process);
    ((QStandardItemModel*)(detailList->model()))->appendRow(item);
}
void ProcessManager::clearDetailInfo(){
    ((QStandardItemModel*)(detailList->model()))->clear();
}
void ProcessManager::revertToOrigin(){
    processes.clear();

    backqueue.clear();
    readyqueue.clear();
    suspendqueue.clear();
    servingqueue.clear();

    backcon->clear();
    readycon->clear();
    suspendcon->clear();

    preReleaseSet.clear();
    preSuspendSet.clear();

    clearProcessTable();
    clearProcessingTable();
    clearDetailInfo();
    timer->stop();
    counting=false;//是否还在计时
    isProcessingTableClean=true;
    int funcCode=-1;
    reloadedJustNow=false;
    initBackQueue();
}
// funcCode 1.开始初始化就绪队列2.开始不断拿取进程到CPU
