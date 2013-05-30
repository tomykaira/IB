#!/home/tomykaira/.rbenv/shims/ruby
#PBS -l nodes=2:ppn=1
#PBS -j oe

job_no = ENV['PBS_JOBID'].split('.', 2).first

STDOUT.reopen(File.join(ENV['PBS_O_WORKDIR'], "output.#{job_no}"), 'w')
STDERR.reopen(File.join(ENV['PBS_O_WORKDIR'], "error.#{job_no}"), 'w')
Dir.chdir(ENV['PBS_O_WORKDIR'])

me = `hostname`.strip
nodes = File.readlines(ENV['PBS_NODEFILE']).map(&:strip)

port = rand(1000) + 7532

nodes.each do |node|
  if node == me
    puts "Spawning server"
    spawn("./receiver_initiated #{port}", [:out, :err] => ["server.#{job_no}", 'a'])
  else
    sleep 1
    puts "Spawning client"
    spawn(%Q{rsh #{node} 'cd #{Dir.pwd}; ./receiver_initiated #{me} #{port}'}, [:out, :err] => ["client.#{job_no}", 'a'])
  end
end

Process.waitall

exit 0
