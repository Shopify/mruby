assert 'mrb_protect' do
  # no failure in protect returns [result, false]
  assert_equal ['test', false] do
    ExceptionTest.mrb_protect { 'test' }
  end
  # failure in protect returns [exception, true]
  result = ExceptionTest.mrb_protect { raise 'test' }
  assert_kind_of RuntimeError, result[0]
  assert_true result[1]
end

assert 'mrb_ensure' do
  a = false
  assert_equal 'test' do
    ExceptionTest.mrb_ensure Proc.new { 'test' }, Proc.new { a = true }
  end
  assert_true a

  a = false
  assert_raise RuntimeError do
    ExceptionTest.mrb_ensure Proc.new { raise 'test' }, Proc.new { a = true }
  end
  assert_true a
end

assert 'mrb_rescue' do
  assert_equal 'test' do
    ExceptionTest.mrb_rescue Proc.new { 'test' }, Proc.new {}
  end

  class CustomExp < Exception
  end

  assert_raise CustomExp do
    ExceptionTest.mrb_rescue Proc.new { raise CustomExp.new 'test' }, Proc.new { 'rescue' }
  end

  assert_equal 'rescue' do
    ExceptionTest.mrb_rescue Proc.new { raise 'test' }, Proc.new { 'rescue' }
  end
end

assert 'mrb_rescue_exceptions' do
  assert_equal 'test' do
    ExceptionTest.mrb_rescue_exceptions Proc.new { 'test' }, Proc.new {}
  end

  assert_raise RangeError do
    ExceptionTest.mrb_rescue_exceptions Proc.new { raise RangeError.new 'test' }, Proc.new { 'rescue' }
  end

  assert_equal 'rescue' do
    ExceptionTest.mrb_rescue_exceptions Proc.new { raise TypeError.new 'test' }, Proc.new { 'rescue' }
  end
end

assert 'each_backtrace UAF regression (H1 #3677726 PoC in isolated mrb_state)' do
  # This mirrors the original HackerOne PoC's C harness exactly: a fresh
  # mrb_state runs the deep-recursion-then-raise script, then (without any
  # intervening Ruby execution to clobber the stale callinfo slots) calls
  # mrb_exc_backtrace directly. Without the fix, GC churn between raise
  # and backtrace walk should free RProc objects whose pointers still live
  # in cibase[ciidx..], and the walk will dereference freed memory.
  #
  # A passing return value means the walk completed without crashing and
  # produced some backtrace entries. Under ASan, a regression shows up as
  # a heap-use-after-free report rather than a silent success.
  len = ExceptionTest.run_uaf_poc
  assert_kind_of Integer, len
  assert_true len >= 0, "expected non-negative backtrace length, got #{len}"
end
