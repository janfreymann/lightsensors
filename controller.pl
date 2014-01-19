use strict;
use Net::OpenSoundControl::Server;

my $server = Net::OpenSoundControl::Server->new(
    Port => 7778, Handler => \&handleMsg) or die
     "Could not start server: $@\n";

print "Tekkalot Game Controller\n";
print "Executing startup script...\n";

system('./startup.sh');

print "Done.\n";

$server->readloop();


sub handleMsg {
  my ($sender, $message) = @_;
  my $path = $$message[0];
  print "$path\n";

  if($path eq "/restart") {
    print('killall combined' . "\n");
    system('killall combined');
    my $ip = $$message[2];
    print('./combined' . " $ip\n");
    system('./combined' . " $ip" . '&');
    print "Restart done.\n";
  }
  elsif($path eq "/shutdown") {
    print('sudo halt');
    system('sudo halt');
    print "Bye.\n";
  }
}
