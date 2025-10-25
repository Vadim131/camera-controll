#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Sun Jul 20 13:15:35 2025

@author: archuser

This is site made in aims to provide images from detector of muon beams

@author Vadim Davilin, v.davilin@g.nsu.ru 
"""

from fastapi import FastAPI, Body, Request, HTTPException, WebSocket, WebSocketException, WebSocketDisconnect
from fastapi.responses import HTMLResponse, StreamingResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles
from datetime import datetime
import uvicorn
import numpy
import asyncio
import json


# my modules
import auth


#constants 
APP_STD_TIMEOUT = 10 #sec

# server
app = FastAPI()
app.state.active_user = None  # will remember user who first press button connect
app.state.task_manager = None
app.state.device = None


app.mount("/static", StaticFiles(directory="static"), name="static")
# templates
templates = Jinja2Templates(directory="templates")

# units of measurement which possibly can be sent to camera
fan_speed_values = ("off", "quiet", "full")
exposure_time_units = {"ms": 1,
                       "sec": 1000,
                       "min": 60*1000,
                       "hour": 60*60*1000,
                       "day": 24*60*60*1000
                       }

class PhotoTask():
    def __init__(self, expos_t: int, num_photos: int):
        """ This is conditions in photo was made """
        
        self.exposure_time = expos_t
        self.photos_amount = num_photos
        print(self.exposure_time, flush=True)

        self.ready_flag = 0
        self.error_flag = 0
        self.cancel_flag = 0
        self.start_time = datetime.now()

    def get_ready_flag(self) -> bool:
        if self.get_progress_percent() == 100:
            #self delete
            app.state.task_manager.current_task = None
            return True
        return False

    def get_error_flag(self) -> bool:
        if self.error_flag:
            #self delete
            app.state.task_manager.current_task = None
        return self.error_flag
    
    def get_cancel_flag(self) -> bool:
        if self.cancel_flag:
            app.state.task_manager.current_task = None
        return self.cancel_flag

    def get_progress_percent(self) -> int:
        if self.cancel_flag:
            return 0
        ms_delta = (datetime.now() - self.start_time).total_seconds()*1000
        progress = int((ms_delta / (self.exposure_time*self.photos_amount))*100)
        # protecting from more than 100% progress in case of error
        return min(100, progress)
    
class TaskManager():
    def __init__(self):
        self.current_task = None
        
    def cancel_task(self, status:int = 0):
        if self.current_task != None:
            if status == 0:
                self.current_task.cancel_flag = 1
            else:
                self.current_task.error_flag = 1
                
    def is_empty(self) -> bool:
        return self.current_task == None
    
    def new_task(self, task: PhotoTask):
        self.current_task = task
        

class Device():
    def __init__(self):
        self.connection_state = asyncio.Event()
        self.new_status_info = asyncio.Event()
        self.params_are_set = asyncio.Event()
        self.photo_task_started = asyncio.Event()
        self.canceled_without_err = asyncio.Event()
        
        self.websocket = None
        self.listener = None
        self._lock = asyncio.Lock()
        self.ccd_temp = None
        self.heat_sink_temp = None
        self.fan_speed = None
               
    def is_connected(self) -> bool:
        return self.websocket is not None
    
    def is_new_status_info(self) -> bool:
        if self.new_status_info.is_set():
            self.new_status_info.clear()
            return True
        return False
    
    def get_status_info(self) -> dict:      
        return {"ccd": self.ccd_temp, "sink": self.heat_sink_temp, "fan": self.fan_speed}
    
    async def _set_websocket(self, websocket: WebSocket) -> bool:
        """ Set active websocket"""
        
        async with self._lock:
            if self.is_connected():
                # Close old
                try:
                    await self.websocket.close(code=1008, reason="New connection established")
                except Exception:
                    return False
            self.websocket = websocket
            return True
            #print("WebSocket assigned to device")
            
    async def _handle_message(self, message):
        if message is not None:
            try:
                data = json.loads(message)
                type_ = data.get("type")
        
                match type_:
                    case "info":  
                        if "ccd" in data and "sink" in data and "fan" in data:
                            self.ccd_temp = data["ccd"]
                            self.heat_sink_temp = data["sink"]
                            self.fan_speed = data["fan"]
                            self.new_status_info.set()
                        
                        elif "ready" in data:
                            pass

                    case "answer":
                        match data["oncommand"]:
                            case "connect":
                                if data["status"] == "success":
                                    self.connection_state.set()
                            case "set":
                                if data["status"] == "success":
                                    self.params_are_set.set()
                            case "disconnect":
                                if data["status"] == "success":
                                    pass
                            case "phototask":
                                if data["status"] == "success":
                                    self.photo_task_started.set()
                            case "cancel":
                                if data["status"] == "success":
                                    self.canceled_without_err.set()
                    case "error":
                        pass
                    case _:
                        pass

            except json.JSONDecodeError as e:
                print(f"Ошибка парсинга JSON: {e}")
            
    async def _listen_messages(self):
        try:
           while self.is_connected():  
               message = await self.websocket.receive_text()
               print(f"Received from camera: {message}", flush=True)
               await self._handle_message(message)
               
        except WebSocketDisconnect:
            print("Camera disconnected!")
        except Exception as e:
            print(f"WebSocket listening error: {e}")
        finally:
            self.websocket = None
            self.ccd_temp = None
            self.heat_sink_temp = None
            self.new_status_info.set()
            app.state.task_manager.cancel_task()
            return
         
            
    async def start_listening(self, websocket: WebSocket):
        """ Creates asyncio task with active websocket to listen it """
        
        if await self._set_websocket(websocket):
            # Запускаем задачу прослушки
            self.listener = asyncio.create_task(self._listen_messages())
            print("WebSocket listening started!", flush=True)
            print(self.is_connected(), flush=True)
            await self.listener
            self.listener = None
            
    async def stop_listening(self):
        if self.is_connected():
            await self.websocket.close(code=1001)
            
         
    async def _send_to_camera(self, text: str) -> bool:
        """ Send text (commands) to camera"""
        
        if not self.is_connected():
            return False
        
        try:
            await self.websocket.send_text(text)
            print(f"Отправка сообщения {text}!", flush=True)
            return True
        except WebSocketException as e:
            print(f"Failed to send text: {e}", flush=True)
            self.websocket = None
            return False    
            
    async def camera_connect(self, timeout: float = APP_STD_TIMEOUT) -> bool:
        """ Send command "connect" to the camera """
        
        self.connection_state.clear()
        try:
            if await asyncio.wait_for(self._send_to_camera("connect"), timeout=timeout):
                print("ЖДЕМ ОТВЕТА ОТ КЛИЕНТА!!", flush=True)
                await asyncio.wait_for(self.connection_state.wait(), timeout=timeout)
                return True
            else:
                return False
        except asyncio.TimeoutError:
            return False
        
    async def camera_set_params(self, fan_speed, target_temp, timeout : float = APP_STD_TIMEOUT) -> bool:
        """ Send command "set" to the camera and params """
        self.params_are_set.clear()
        try:
            command = f"set {fan_speed} {target_temp}"
            if await asyncio.wait_for(self._send_to_camera(command), timeout=timeout):
                await asyncio.wait_for(self.params_are_set.wait(), timeout=timeout)
                return True
            else:
                return False
        except asyncio.TimeoutError:
            return False
        
    async def camera_cancel_task(self, timeout : float = APP_STD_TIMEOUT) -> bool:
        """ Send command 'cancel' to the camera """
        self.canceled_without_err.clear()
        try:
            command = "cancel"
            if await asyncio.wait_for(self._send_to_camera(command), timeout):
                    
                print("ЖДЕМ ОТВЕТА ОТ КЛИЕНТА!!", flush=True)
                await asyncio.wait_for(self.canceled_without_err.wait(), timeout=timeout)
                return True
            else:
                return False
        except asyncio.TimeoutError:
            return False
            
        
    async def camera_send_task(self, task: PhotoTask, timeout : float = APP_STD_TIMEOUT) -> bool:
        """ Send command "phototask" to the camera and photo task"""
        self.photo_task_started.clear()
        try:
            command = f"phototask {task.exposure_time} {task.photos_amount}"
            if await asyncio.wait_for(self._send_to_camera(command), timeout):
                    
                print("ЖДЕМ ОТВЕТА ОТ КЛИЕНТА!!", flush=True)
                await asyncio.wait_for(self.photo_task_started.wait(), timeout=timeout)
                return True
            else:
                return False
        except asyncio.TimeoutError:
            return False
        return True #TODO

app.state.device = Device()
app.state.task_manager = TaskManager()


def read_page(path: str) -> str:
    """ This is for reading static html pages """
    
    try:
        with open(path, "r") as fd:
            page_content = fd.read()
    except IOError:
        page_content = "<h2>Sorry, something went wrong...</h2>"
    return page_content

# ---- Routes ----
@app.get("/")
async def root_page(request: Request):
    is_authenticated, email = auth.check_auth(request)
    if is_authenticated:
        return HTMLResponse(content=read_page("static/mainpage.html"))
    else:
        return RedirectResponse(url="/login-with-otp", status_code=303)


@app.post("/send-otp")
async def send_otp(data=Body()):
    email = data["email"]

    # TODO check if mail is valid!
    if auth.create_and_send_otp(email):
        return {"message": "Code is sent to email"}
    raise HTTPException(status_code=500, detail="Failed to send email")


@app.get("/verify-otp")
async def login_verify():
    return HTMLResponse(content=read_page("static/login-verify-otp.html"))


@app.get("/login-with-otp")
async def login_page():
    return HTMLResponse(content=read_page("static/login-get-otp.html"))


@app.post("/login")
async def login_otp(data=Body()):
    email = data["email"]
    otp = data["otp"]

    status = auth.verify_otp(email, otp)

    if True == status:
        # create token and login
        token = auth.create_access_token(email)

        response = HTMLResponse(
            content="Successfully authorized!", status_code=200)
        response.set_cookie(
            key="access_token",
            value=token,
            httponly=True,
            max_age=None,
            samesite="Lax",
            path="/"
        )
        return response
    else:
        raise HTTPException(
            status_code=400, detail="The code is wrong or expired")

        
@app.post("/logout")
async def logout(request: Request):
    is_authenticated, email = auth.check_auth(request)
    if is_authenticated:
        response = RedirectResponse(url="/login-with-otp", status_code=303)
        response.set_cookie(
            key="access_token",
            httponly=True,
            max_age=None,
            samesite="Lax",
            path="/"
        )
        return response
    else:
        raise HTTPException(status_code=401, detail="Unauthorized!")
    

@app.get("/profile")
async def profile(request: Request):
    is_authenticated, email = auth.check_auth(request)
    if is_authenticated:
        act_mail = app.state.active_user + " (you)" if email == app.state.active_user else app.state.active_user
        data = {"usrmail": email,
                "activemail": act_mail
                }

        return templates.TemplateResponse(
            request=request,
            name="profile.html",
            context={"data": data}
        )
    else:
        return RedirectResponse(url="/login-with-otp", status_code=303)
    
#TODO server photo

@app.post("/connect")
async def camera_connect(request: Request):
    is_authenticated, email = auth.check_auth(request)
    if is_authenticated:
        # add user's email in app.state.active_user or cancel his request
        if None == app.state.active_user:
            app.state.active_user = email
            
            success = await app.state.device.camera_connect()
            if success:
                return HTMLResponse(content="Success!", status_code=200)
            else:
                app.state.active_user = None
                return HTMLResponse(content="Something wrong! Failed camera connection!", status_code=500)
        elif email == app.state.active_user:
            return HTMLResponse(content="You are already connected to the camera!", status_code=409)
        else:
            return HTMLResponse(content=f'Camera is in use by {app.state.active_user}!', status_code=423)
    raise HTTPException(status_code=401, detail="Unauthorized!")


@app.post("/disconnect")
async def camera_disconnect(request: Request):
    is_authenticated, email = auth.check_auth(request)
    if is_authenticated:
        # delete user's email from app.state.active_user 
        if email == app.state.active_user:
            app.state.active_user = None
            return HTMLResponse(content="Success!", status_code=200)
        elif None == app.state.active_user:
            return HTMLResponse(content="You are already not connected to the camera!", status_code=400)
        else:
            return HTMLResponse(content=f'Camera is in use by {app.state.active_user}!', status_code=423)
    raise HTTPException(status_code=401, detail="Unauthorized!")


@app.post("/set-params")
async def camera_set_params(request: Request, body=Body()):
    is_authenticated, email = auth.check_auth(request)
    if is_authenticated:
        if app.state.active_user == email:
            
            # validation
            try:
                target_temp = int(body["target_temp"])
                fan_speed = body["fan_speed"].lower()
                print(target_temp, fan_speed, flush=True)
                
                if fan_speed in fan_speed_values and 0 <= target_temp <= 30:
                    if await app.state.device.camera_set_params(fan_speed, target_temp):  
                        return HTMLResponse(content="Success! Params are set! Monitor status on the right panel!", status_code=200)
                    return HTMLResponse(content="Something wrong! Failed to set params!", status_code=500)
            except (TypeError, ValueError):
                return HTMLResponse(content="Wrong params! Target temp should be in [0, 30] and fan value from box!", status_code=400)
        elif None == app.state.active_user:
            return HTMLResponse(content="Connect to the camera firstly!", status_code=400)
        else:
            return HTMLResponse(content=f'Camera is in use by {app.state.active_user}!', status_code=423)
    raise HTTPException(status_code=400, detail="Unauthorized")


@app.post("/make-photo")
async def camera_make_photo(request: Request, body=Body()):
    is_authenticated, email = auth.check_auth(request)
    if is_authenticated:
        if app.state.active_user == email:
            
            if not app.state.task_manager.is_empty():
                return HTMLResponse(content="Photo is making. Cancel ot firstly", status_code=400)
            # validation
            try:
                num_photos = int(body["num_photos"])
                exp_time_value = int(body["exposure_value"])
                exp_time_unit = body["exposure_unit"]

                if 0 < num_photos <= 15 and exp_time_unit in exposure_time_units.keys():
                        
                    task = PhotoTask(exp_time_value*exposure_time_units[exp_time_unit], num_photos)
                    if await app.state.device.camera_send_task(task):  
                        app.state.task_manager.new_task(task)
                        
                        return HTMLResponse(content="Success! Starting camera streaming", status_code=200)
                    return HTMLResponse(content="Something wrong! Failed starting photo task!", status_code=500)
            except (TypeError, ValueError):
                pass
            return HTMLResponse(content="Wrong params!", status_code=400)
        elif None == app.state.active_user:
            return HTMLResponse(content="Connect to the camera firstly!", status_code=400)
        else:
            return HTMLResponse(content=f'Camera is in use by {app.state.active_user}!', status_code=423)
    raise HTTPException(status_code=400, detail="Unauthorized")

@app.post("/cancel-photo")
async def cancel_photo(request: Request):
    is_authenticated, email = auth.check_auth(request)
    if is_authenticated:
        # delete user's email from app.state.active_user 
        if email == app.state.active_user:
            app.state.task_manager.cancel_task()
            
            if await app.state.device.camera_cancel_task():
                return HTMLResponse(content="Success!", status_code=200)
            else:
                return HTMLResponse(content="Something wrong! Camera failure!", status_code=500)
        elif None == app.state.active_user:
            return HTMLResponse(content="You are not connected to the camera!", status_code=400)
        else:
            return HTMLResponse(content=f'Camera is in use by {app.state.active_user}!', status_code=423)
    raise HTTPException(status_code=401, detail="Unauthorized!")

""" Stream use events to signal which type of msg it is;
    event types: "status", "failure", "processing", "finished" """
    
def sse_format(event: str, data: dict):
    return f"event: {event}\ndata: {json.dumps(data)}\n\n"


@app.get("/stream")
async def status_stream(request: Request):
    is_authenticated, email = auth.check_auth(request)
    if not is_authenticated:
        raise HTTPException(status_code=400, detail="Unauthorized")
    #else   
    async def event_generator():
        while True:
            if await request.is_disconnected():
                break

            if app.state.device.is_new_status_info():
                info = app.state.device.get_status_info()
                data = {
                    "online": app.state.device.is_connected(),
                    "ccd_temp": info["ccd"],
                    "heat_sink_temp": info["sink"],
                    "fan_status": info["fan"]
                }
                yield sse_format("status", data)
               
            if app.state.task_manager.current_task != None:
                if app.state.task_manager.current_task.get_ready_flag():
                    # Задача завершена успешно
                    result_url = "/static/photo.jpg"  # например: "/photos/latest.jpg"
                    data = {"photo_url": result_url}
                    yield sse_format("finished", data)
    
                elif app.state.task_manager.current_task.get_error_flag():
                    data = {"error": "Photo task failed!"}
                    yield sse_format("failure", data)
                    
                elif app.state.task_manager.current_task.get_cancel_flag():
                    pass 
    
                else:
                    progress = app.state.task_manager.current_task.get_progress_percent()
                    data = {"progress": progress}
                    yield sse_format("processing", data)
                    
            await asyncio.sleep(5)
            

    return StreamingResponse(
        event_generator(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
            "Access-Control-Allow-Origin": "*",
        }
    )
    
@app.websocket("/ws/raspberry")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    
    first_message = await websocket.receive_text()
    data = json.loads(first_message)
    
    if data.get("type") == "onconnection" and auth.check_ws_key(data.get("key")):
        try:
            # закрыть предыдущее
            await app.state.device.stop_listening()
            # Вся основная логика в Device
            await app.state.device.start_listening(websocket)
                
        except WebSocketException as e:
            print(f"WebSocket endpoint error: {e}")
    else:
        await websocket.close(code=4000, reason="Invalid key")
        

if __name__ == "__main__":

    uvicorn.run(
        "main:app",
        host="127.0.0.1",
        port=8000,
        reload=True,
        log_level="info",           # ← ВАЖНО
        access_log=True             # ← Показывать запросы
    )
