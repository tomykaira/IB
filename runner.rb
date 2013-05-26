#!/home/tomykaira/.rbenv/shims/ruby
#PBS -l nodes=2:ppn=1
#PBS -j oe

job_no = ENV['PBS_JOBID'].split('.', 2).first

STDOUT.reopen(File.join(ENV['PBS_O_WORKDIR'], "output.#{job_no}"), 'w')
STDERR.reopen(File.join(ENV['PBS_O_WORKDIR'], "error.#{job_no}"), 'w')
Dir.chdir(ENV['PBS_O_WORKDIR'])

me = `hostname`.strip
nodes = File.readlines(ENV['PBS_NODEFILE']).map(&:strip)

16.times do |x|
  port = rand(1000) + 7532
  size = (x + 1) * 1000

  nodes.each do |node|
    if node == me
      puts "Spawning server"
      spawn("./bench #{port} #{size}", [:out, :err] => ["server.#{job_no}", 'a'])
    else
      sleep 1
      puts "Spawning client"
      spawn(%Q{rsh #{node} 'cd #{Dir.pwd}; ./bench #{me} #{port} #{size}'}, [:out, :err] => ["client.#{job_no}", 'a'])
    end
  end

  Process.waitall
end

exit 0
