#pragma once
#include <stdint.h>
#include <kapi.h>

template <class T>
class vector {
public:
    vector();
    vector(const vector<T>& v);
    void push_back(T item);
    T pop_back();
    void push_front(T item);
    T pop_front();
    uint32_t size();
    void remove(uint32_t index);

     T & operator[](uint32_t index);
     vector<T> & operator=(const vector<T>& v);

    ~vector();

private:
    T* _items;
    uint32_t itemCount = 0;
    bool _init = false;
};

template<class T>
vector<T>::vector() {
    _items = (T*)kapi::api.mm.malloc(sizeof(T));
    itemCount = 0;
    _init = true;
}

template<class T>
vector<T>::vector(const vector<T>& v) {
    _items = (T*)kapi::api.mm.malloc(sizeof(T) * v.itemCount);
    kapi::api.mm.memcpy(_items, v._items, sizeof(T) * v.itemCount);
    itemCount = v.itemCount;
    _init = true;
}

template<class T>
vector<T>::~vector() {
    //delete[] _items;
    // TODO: FIX ALL THIS CRAP (use new to init items and fix delete)
    kapi::api.mm.free(_items);
    _items = (T*)0;
    itemCount = 0;
    _init = true;
}

template<class T>
void vector<T>::push_back(T item) {
    T* temp = new T[itemCount + 1];
    for (int i = 0; i < itemCount; i++) {
        temp[i] = _items[i];
    }
    temp[itemCount] = item;
    itemCount++;
    _items = temp;
}

template<class T>
T vector<T>::pop_back() {
    if (itemCount == 0) {
        return NULL;
    }
    itemCount--;
    T item = _items[itemCount];
    _items = (T*)kapi::api.mm.realloc(_items, itemCount * sizeof(T));
    return item;
}

template<class T>
void vector<T>::push_front(T item) {
    itemCount++;
    _items = (T*)kapi::api.mm.realloc(_items, itemCount * sizeof(T));
    kapi::api.mm.memcpy(_items + sizeof(T), _items, (itemCount - 1) * sizeof(T));
    _items[0] = item;
}

template<class T>
T vector<T>::pop_front() {
    if (itemCount == 0) {
        return NULL;
    }
    itemCount--;
    T item = _items[0];
    kapi::api.mm.memcpy(&_items[0], &_items[1], itemCount * sizeof(T));
    _items = (T*)kapi::api.mm.realloc(_items, itemCount * sizeof(T));
    return item;
}

template<class T>
void vector<T>::remove(uint32_t index) {
    kapi::api.mm.memcpy(&_items[index], &_items[index + 1], (itemCount - (index + 1)) * sizeof(T));
    itemCount--;
    _items = (T*)kapi::api.mm.realloc(_items, itemCount * sizeof(T));
}

template<class T>
uint32_t vector<T>::size() {
    return itemCount;
}

template<class T>
T& vector<T>::operator[](uint32_t index) {
    return _items[index];
}

template<class T>
vector<T>& vector<T>::operator=(const vector<T>& v) {
    if (_init) {
        kapi::api.mm.free(_items);
    }
    _items = (T*)kapi::api.mm.malloc(sizeof(T) * v.itemCount);
    kapi::api.mm.memcpy(_items, v._items, sizeof(T) * v.itemCount);
    itemCount = v.itemCount;
    return *this;
}