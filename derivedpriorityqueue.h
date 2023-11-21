#ifndef DERIVEDPRIORITYQUEUE_H
#define DERIVEDPRIORITYQUEUE_H
#include <queue>
template <class _Ty, class _Container = std::deque<_Ty>, class _Pr = std::less<typename _Container::value_type>>
class DerivedPriorityQueue : public std::priority_queue<_Ty, _Container, _Pr> {
public:
    bool remove(const  _Ty& value) {
        auto it = std::find(this->c.begin(), this->c.end(), value);
        if (it != this->c.end()) {
            this->c.erase(it);
            std::make_heap(this->c.begin(), this->c.end(), this->comp);
            return true;
        }
        else {
            return false;
        }
    }
    bool erase(int index) {
        if (index < this->c.size()) {
            this->c.erase(this->c.begin() + index);
            std::make_heap(this->c.begin(), this->c.end(), this->comp);
            return true;
        }
        else {
            return false;
        }
    }
    //注意优先队列实质是堆，所以只能保证堆顶元素是最值，所以不能靠获取优先队列内部元素，来寻找第几小（大）的元素,所以获取内部元素的顺序没有意义，故不提供。
    //如果考虑“使用动态数组，每次有元素变动都重新排序”这个方案，每次都要最少O(nlogn)的复杂度，但是顺序读取是O(1+n)=O(n)的复杂度，
    //而优先队列,每次变动元素(指的是顶部元素变动),调整堆是O(logn)的复杂度，(这里不考虑删除非顶部的元素，此时相当于重新建堆，O(n),但是这种情况在这个实验很少)，但是顺序获取，复杂度是O(nlogn+n),
    //综合考量，采用优先队列
    //调用get之前务必要用size检查
    //_Ty get(size_t index){
    //  return *(this->c.begin()+index);
    //}
    void clear(){
        this->c.clear();
    }
};
#endif // DERIVEDPRIORITYQUEUE_H
