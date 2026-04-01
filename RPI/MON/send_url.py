import socket, requests, psutil

URL = "https://ntfy.sh/VAPS"

def get_ip():
    # 라즈베리파이의 무선랜 이름인 'wlan0'을 직접 찾습니다
    addrs = psutil.net_if_addrs().get('wlan0')
    
    if addrs:
        for addr in addrs:
            if addr.family == socket.AF_INET: # IPv4 주소만
                return addr.address
    return "0.0.0.0"

def send():
    ip = get_ip()
    msg = f"MON system url: http://{ip}:5000"
    try:
        requests.post(URL, data=msg.encode("utf-8"))
        print("[sent]", msg)
    except Exception as e:
        print("[err]", e)
    return ip

if __name__ == '__main__':
    send()