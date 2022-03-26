/* -*- Mode: C++ -*- */

#ifndef _STACK_H
#define _STACK_H

#include "Common.H"
#include "exec_calls.h"
#include "om.h"
#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include <cstring> /// memset
#include <inttypes.h>
#include <pthread.h>
#include <string.h>

//#include "debug_util.h"

// TB 20130123: I'm using my own custom stack type to let me
// performance engineer this later.
/*
 * Stack data structure for storing and maintaining data
 * associated with the call stack.
 */
template <typename STACK_DATA_T> class Stack_t {
protected:
  /* Default capacity for call stack.  Tunable to minimize
   * resizing. */
  static const uint32_t DEFAULT_CAPACITY = 128;

  /* call stack, implemented as an array of STACK_DATA_T's */
  STACK_DATA_T* _stack;
  /* current capacity of call stack */
  uint32_t _capacity;
  /* current head of call stack */
  uint32_t _head;
  uint32_t _tail;

  /* General method to resize the call stack.
   * Called by _double_cap() and _halve_cap().
   *
   * @param new_capacity New capacity of the call stack.
   */
  void _resize(uint32_t new_capacity) {
    // Save a pointer to the call stack
    STACK_DATA_T* old_stack = _stack;
    // Allocate new call stack array
    _stack = new STACK_DATA_T[new_capacity];
    // Determine amount to copy over
    uint32_t copy_end = _capacity > new_capacity ? new_capacity : _capacity;

    // Copy contents of old call stack
    for (int i = 0; i < copy_end; ++i) {
      _stack[i] = old_stack[i];
    }
    _capacity = new_capacity;

    // Delete old call_stack
    delete[] old_stack;
  }

  /*
   * Doubles the capacity of the call stack.
   */
  void _double_cap() { _resize(_capacity * 2); }

  /*
   * Halves the capacity of the call stack.
   */
  void _halve_cap() { _resize(_capacity / 2); }

public:
  /*
   * Default constructor.
   */
  Stack_t() : _capacity(DEFAULT_CAPACITY), _head((uint32_t)-1), _tail((uint32_t)0) {
    _stack = new STACK_DATA_T[_capacity];
  }

  /*
   * Destructor.
   */
  ~Stack_t() { delete[] _stack; }

  /*
   * Reset, effectively removing all data.
   */
  void reset() {
    _head = (uint32_t)-1;
    _tail = (uint32_t)0;
  }

  /*
   * Simulate entering a function.  Effectively pushes a new
   * STACK_DATA_T onto the head of the call stack.
   */
  void push() {
    ++_head;

    if (_head == _capacity) {
      _double_cap();
    }
  }

  /*
   * Simulate exiting a function.  Effectively pops the head
   * STACK_DATA_T off of the stack.
   */
  void pop() {
    --_head;
    if (_capacity > DEFAULT_CAPACITY && _head < _capacity / 2) {
      _halve_cap();
    }
  }

  /*
   * Retrieves an arbitrary ancestor's STACK_DATA_T, specifically a
   * pointer to that data on the call stack.
   *
   * @param i the ancestor for the call at the head of the stack,
   * where i = 0 indicates the head of the call stack.
   */
  STACK_DATA_T* ancestor(uint32_t i) const {
    assert(i <= _head);
    assert(_head < _capacity);
    return &(_stack[_head - i]);
  }

  /*
   * Retrieves a STACK_DATA_T at index i, specifically a
   * pointer to that data on the call stack.
   *
   * @param i the index of the stack element,
   * where element at index 0 is the oldest element.
   */
  STACK_DATA_T* at(uint32_t i) const {
    assert(i >= 0 && i <= _head);
    assert(_head < _capacity);
    return &(_stack[i]);
  }

  /*
   * Retrieves the STACK_DATA_T at the head of the call stack.
   */
  STACK_DATA_T* head() const { return ancestor(0); }

  /*
   * Returns the current size of the stack, i.e. the number of entries
   * on the stack.
   */
  uint32_t size() const { return _head + 1; }

  bool empty() const { return size() == 0; }
};

// Simple atomic class
template <typename T> class AtomicStack_t : public Stack_t<T> {
private:
  pthread_spinlock_t _slock;

public:
  AtomicStack_t() { pthread_spin_init(&_slock, PTHREAD_PROCESS_PRIVATE); }
  ~AtomicStack_t() { pthread_spin_destroy(&_slock); }
  void push_helper() {
    pthread_spin_lock(&_slock);
    assert(this->_head != (uint32_t)-1);
    assert(!(Stack_t<T>::head()->flags & FRAME_HELPER_MASK));
    Stack_t<T>::push();
    Stack_t<T>::head()->flags = FRAME_HELPER_MASK;
    pthread_spin_unlock(&_slock);
  }

  void push() {
    pthread_spin_lock(&_slock);
    Stack_t<T>::push();
    pthread_spin_unlock(&_slock);
  }

  void add(T item) {
    pthread_spin_lock(&_slock);
    Stack_t<T>::push();
    *(Stack_t<T>::head()) = item;
    pthread_spin_unlock(&_slock);
  }

  void reset() {
    pthread_spin_lock(&_slock);
    /// This is inefficient, but REALLY helps with debugging
    memset((void*)this->_stack, 0xff, sizeof(T) * this->size());
    Stack_t<T>::reset();
    pthread_spin_unlock(&_slock);
  }

  void pop() {
    pthread_spin_lock(&_slock);
    assert(this->_head >= this->_tail);
    Stack_t<T>::pop();

    if (this->_head < this->_tail) {
      // note that this path won't be taken if _head == -1 since it's an
      // unsigned int, so the comparison expr uses unsigned comparison
      this->_tail--;
      assert(this->_tail == this->_head);
      memset((void*)&this->_stack[this->_head + 1], 0xff, sizeof(T));
    }
    assert(this->_head >= 0);
    pthread_spin_unlock(&_slock);
  }

  T* steal_top(AtomicStack_t<T>& thief) {
    pthread_spin_lock(&_slock);
    assert(this->_tail != (uint32_t)-1);
    assert(this->_head >= this->_tail);
    assert(thief.empty());

    // Take victim's tail
    thief._tail = this->_tail;

    // Copy up to the tail
    for (uint32_t i = 0; i < this->_tail; ++i) {
      thief.push();
      *(thief.head()) = this->_stack[i];
      thief.head()->flags |= FRAME_FULL_MASK;
    }

    // Now copy up to next HELPER frame
    // Note: there MUST be another helper frame, since stealing is possible
    T* f = &this->_stack[this->_tail];
    thief.push();
    *(thief.head()) = *f;
    f = &this->_stack[++this->_tail];

    while (!(f->flags & FRAME_HELPER_MASK)) {
      assert(this->_tail < this->_head);
      //      assert(this->_tail <= this->_head);
      thief.push();
      *(thief.head()) = *f;
      f = &this->_stack[++this->_tail];
    }
    assert(f->flags & FRAME_HELPER_MASK);
    assert(this->_tail <= this->_head);

    pthread_spin_unlock(&_slock);
    return thief.head();
  }

  void transfer(AtomicStack_t<T>& orig) {
    // Not true, b/c we currently only reset on a successful steal...
    //    assert(orig.empty());
    pthread_spin_lock(&_slock);

    orig.reset();
    for (int i = 0; i <= this->_head; ++i) {
      orig.push();
      *(orig.head()) = this->_stack[i];
    }
    orig._tail = this->_tail;
    pthread_spin_unlock(&_slock);
  }

  size_t memsize() { return (this->_capacity * sizeof(T)) + sizeof(*this); }
};

#endif // #define _STACK_H
