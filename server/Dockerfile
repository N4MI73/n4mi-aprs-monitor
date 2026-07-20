FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY aprsmon_server.py .

EXPOSE 8079

CMD ["python", "aprsmon_server.py"]
