import { SLPServer } from './udpserver'
import { ServerMonitor } from './monitor'
import { AuthProvider, HttpAuthProvider, JsonAuthProvider } from './auth'
import { CustomAuthProvider } from "./auth/CustomAuthProvider";

function parseArgs2Obj(args: string[]) {
  let argsObj: any = {};
  for (let i = 0; i < args.length; i += 2) {
    let key: string = args[i];
    let value: string = args[i + 1];
    if (key.startsWith('--')) {
      argsObj[key.slice(2)] = value;
    }
  }
  return argsObj;
}

function main(argv: string[]) {
  console.log("DEBUG: main() called with:", argv);
  let argsObj = parseArgs2Obj(argv);

  let provider: AuthProvider | undefined
  const {USE_HTTP_PROVIDER} = process.env
  if (USE_HTTP_PROVIDER) {
    provider = new HttpAuthProvider(USE_HTTP_PROVIDER)
    console.log(`using HttpAuthProvider url: ${USE_HTTP_PROVIDER}`)
  } else if (argsObj.httpAuth) {
    provider = new HttpAuthProvider(argsObj.httpAuth)
    console.log(`using HttpAuthProvider url: ${argsObj.httpAuth}`)
  } else if (argsObj.jsonAuth) {
    provider = new JsonAuthProvider(argsObj.jsonAuth)
    console.log(`using JsonAuthProvider file: ${argsObj.jsonAuth}`)
  } else if (argsObj.simpleAuth) {
    let username_password = argsObj.simpleAuth.split(':');
    let username = username_password[0];
    let password = username_password[1];
    provider = new CustomAuthProvider(username, password);
    console.log(`using simple auth with username=${username} password=${password}`);
  }
  const portNum = parseInt(argsObj.port || '11451')
  const protocol = argsObj.protocol || 'udp'
  console.log(`[Data Transport] SLP server started on port ${portNum} using ${protocol.toUpperCase()} protocol`)
  let s = new SLPServer(portNum, provider, protocol)
  let monitor = new ServerMonitor(s)
  // TCPモード時の衝突を避けるため、監視ポートを別にする
  const monitorPort = (protocol === 'tcp') ? (parseInt(argsObj.monitorPort || String(portNum + 1))) : portNum;
  monitor.start(monitorPort)
}

main(process.argv.slice(2))
