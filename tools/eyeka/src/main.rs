use hidapi::HidApi;
use pimax_protocol::cmd::{Command, Query};
use pimax_protocol::cmd::eye_track_enable::CmdEyeTrackEnable;
use pimax_protocol::cmd::pc_heartbeat::CmdPcHeartbeat;
use pimax_protocol::device::PimaxSubDevice;
use pimax_protocol::{CmdResult, CommandDevice};
use std::{thread::sleep, time::{Duration, Instant}};

/// Raw Vision (sub-device 0x05) command with arbitrary payload.
struct Raw<const ID: u8>(Vec<u8>);
impl<const ID: u8> Command for Raw<ID> {
    const ID: u8 = ID;
    fn cmd_opt(&self) -> PimaxSubDevice { PimaxSubDevice::Vision }
    fn write_payload(&self, out: &mut Vec<u8>) { out.extend_from_slice(&self.0) }
}
impl<const ID: u8> Query for Raw<ID> {
    type Response = Vec<u8>;
    fn decode(r: CmdResult) -> pimax_protocol::Result<Vec<u8>> { Ok(r.payload) }
}
fn q<const ID: u8>(cmd: &CommandDevice, name: &str, p: &[u8]) {
    let r = cmd.query(&Raw::<ID>(p.to_vec()));
    println!("{name} {:02x?} -> {:02x?}", p, r.map_err(|e| format!("{e:?}")));
    sleep(Duration::from_millis(5));
}
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let secs: u64 = a.get(1).and_then(|s| s.parse().ok()).unwrap_or(30);
    let replay = a.iter().any(|x| x == "--replay");   // Vision (opt 0x05) bring-up, no panel/level/licence cmds
    let api = HidApi::new().unwrap();
    let cmd = CommandDevice::open(&api).expect("open command device");
    if replay {
        q::<0x3A>(&cmd, "vision_sts", &[0x03]); q::<0x3A>(&cmd, "vision_sts", &[0x01]); q::<0x3A>(&cmd, "vision_sts", &[0x00]);
        q::<0x56>(&cmd, "opt56", &[0x0a]); q::<0x56>(&cmd, "opt56", &[0x0b, 0, 0, 1]);
        q::<0x1B>(&cmd, "opt1b", &[0x01]);
        q::<0x5B>(&cmd, "opt5b", &[0x02, 0x02]); q::<0x5A>(&cmd, "opt5a", &[0x00]);
    }
    let t0 = Instant::now(); let mut n = 0;
    while t0.elapsed().as_secs() < secs {
        let r = cmd.query(&CmdEyeTrackEnable);
        if n % 10 == 0 { println!("[{:5.1}s] eye enable -> {:02x?}", t0.elapsed().as_secs_f32(), r.map_err(|e| format!("{e:?}"))); }
        let _ = cmd.send(&CmdPcHeartbeat { required_video_rate: 0 });
        n += 1; sleep(Duration::from_millis(500));
    }
}
