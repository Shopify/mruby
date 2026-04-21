# Build config for building mruby under AddressSanitizer to help
# surface memory safety issues (use-after-free, heap overflow, etc.)
# in tests and tools.
#
# Usage:
#   MRUBY_CONFIG=$(pwd)/asan_config.rb rake clean
#   MRUBY_CONFIG=$(pwd)/asan_config.rb rake test
#
# Note: `rake clean test` in a single invocation fails because
# mrbgems/mruby-test/mrbgem.rake writes build/test/active_gems.lst
# eagerly at rakefile-load time; `clean` then deletes it, and nothing
# regenerates it later in the same invocation. Run the two tasks as
# separate rake calls.

def detect_toolchain
  cc = ENV['CC'] || 'cc'
  `#{cc} --version 2>&1` =~ /clang/i ? :clang : :gcc
end

MRuby::Build.new('test') do |conf|
  toolchain detect_toolchain

  enable_debug
  conf.enable_bintest
  conf.enable_test

  conf.gembox 'default'

  asan_flags = %w[-fsanitize=address -fno-omit-frame-pointer -g -O1]
  conf.cc.flags.concat asan_flags
  conf.cxx.flags.concat asan_flags if conf.respond_to?(:cxx)
  conf.linker.flags.concat asan_flags
end

MRuby::Build.new('host') do |conf|
  toolchain detect_toolchain
  conf.gembox 'default'

  asan_flags = %w[-fsanitize=address -fno-omit-frame-pointer -g -O1]
  conf.cc.flags.concat asan_flags
  conf.linker.flags.concat asan_flags
end
